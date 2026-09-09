"""Read actual FBX payloads and geometry; preserve cockpit interface at export boundary."""
from pathlib import Path
import bpy,json,math,hashlib,os
import numpy as np
from mathutils import Vector
from io_scene_fbx import parse_fbx as parse
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'Content/Star/Art/CockpitV3';ART=ROOT/'work/cockpit-v04'
CONTEXT=Path(os.environ.get('STAR_CONTEXT_ROOT',str(ROOT)))
manifest=json.loads((OUT/'manifest_v04.json').read_text(encoding='utf-8'))
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def fbx_arrays(path):
 root,version=parse.parse(str(path));values={};raw={}
 def visit(e):
  if e.id in (b'Normals',b'NormalsIndex',b'Tangents',b'Binormals',b'Vertices',b'PolygonVertexIndex',b'UV',b'UVIndex',b'Materials'):raw[e.id]=np.asarray(e.props[0])
  if e.id in (b'Normals',b'Tangents',b'Binormals'):
   key=e.id.decode();values.setdefault(key,[]).extend(float(x) for x in e.props[0])
  for child in e.elems:visit(child)
 visit(root)
 n=raw[b'Normals'].reshape(-1,3)[raw[b'NormalsIndex']];t=raw[b'Tangents'].reshape(-1,3)
 dot=np.einsum('ij,ij->i',n,t);bad=np.flatnonzero(abs(dot)>.01);polys=np.unique(bad//3)
 ix=raw[b'PolygonVertexIndex'];ix=np.where(ix<0,-ix-1,ix);v=raw[b'Vertices'].reshape(-1,3)[ix].reshape(-1,3,3)
 uv=raw[b'UV'].reshape(-1,2)[raw[b'UVIndex']].reshape(-1,3,2)
 area=np.linalg.norm(np.cross(v[:,1]-v[:,0],v[:,2]-v[:,0]),axis=1)/2
 d=uv[:,1]-uv[:,0];e=uv[:,2]-uv[:,0];uvarea=abs(d[:,0]*e[:,1]-d[:,1]*e[:,0])/2
 basis={'max_abs_n_dot_t':float(abs(dot).max()),'p99_abs_n_dot_t':float(np.percentile(abs(dot),99)),'nonorthogonal_loops_threshold_001':len(bad),'loops':len(n),'affected_triangles':len(polys),'degenerate_uv_on_affected':int((uvarea[polys]<1e-14).sum()),'degenerate_geometry_on_affected':int((area[polys]<1e-14).sum()),'nondegenerate_affected_triangles':int((area[polys]>=1e-14).sum()),'affected_material_slot_triangles':{str(int(k)):int((raw[b'Materials'][polys]==k).sum()) for k in np.unique(raw[b'Materials'][polys])},'action':'Raw exporter bytes preserved; engine imports normals, removes degenerate faces and recomputes MikkTSpace tangents'}
 return {'version':version,'basis_diagnostic':basis,'arrays':{k:{'scalar_count':len(v),'finite':all(math.isfinite(x) for x in v),'length_min_max':[min(math.sqrt(sum(x*x for x in v[i:i+3])) for i in range(0,len(v),3)),max(math.sqrt(sum(x*x for x in v[i:i+3])) for i in range(0,len(v),3))]} for k,v in values.items()}}
checks=[]
for p in manifest['model_parts']:
 path=OUT/p['fbx'];before=set(bpy.data.objects);bpy.ops.import_scene.fbx(filepath=str(path),use_custom_normals=True)
 objs=[o for o in bpy.data.objects if o not in before and o.type=='MESH'];verts=[o.matrix_world@v.co for o in objs for v in o.data.vertices]
 bounds={'min':[min(v[i] for v in verts) for i in range(3)],'max':[max(v[i] for v in verts) for i in range(3)]}
 for o in objs:o.data.calc_loop_triangles()
 tris=sum(len(o.data.loop_triangles) for o in objs);arrays=fbx_arrays(path)
 checks.append({'part':p['name'],'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'finite':all(math.isfinite(c) for v in verts for c in v),'triangles':tris,'authored_triangles':p['triangles'],'triangle_count_match':tris==p['triangles'],'baseline_triangles':manifest['baseline_triangles'][p['name']],'triangle_delta':tris-manifest['baseline_triangles'][p['name']],'bounds_m':bounds,'uv_present':all(bool(o.data.uv_layers) for o in objs),'expected_meter_scale':4<bounds['min'][0]<7 and bounds['max'][0]<8.5,'fbx_vector_payload':arrays,'normal_tangent_binormal_present':all(k in arrays['arrays'] for k in ('Normals','Tangents','Binormals')),'all_vector_values_finite':all(x['finite'] for x in arrays['arrays'].values())})
deps=bpy.context.evaluated_depsgraph_get();camera=Vector((6,0,1.3));rays=[]
for idx,cy in enumerate((-.88,0,.88)):
 blocked=[]
 for u in (-.98,-.75,-.5,0,.5,.75,.98):
  for v in (-.98,-.75,-.5,0,.5,.75,.98):
   target=Vector((6.838,cy+u*.619/2,.747+v*.312/2));d=target-camera
   hit,loc,n,face,obj,mat=bpy.context.scene.ray_cast(deps,camera,d.normalized(),distance=d.length-.0005)
   if hit:blocked.append({'u':u,'v':v,'object':obj.name,'hit_m':list(loc)})
 rays.append({'screen':idx,'sample_count':49,'blocked':blocked})
required=('finite','triangle_count_match','uv_present','expected_meter_scale','normal_tangent_binormal_present','all_vector_values_finite')
report={'status':'LOCAL_GEOMETRY_PASS' if all(all(c[k] for k in required) for c in checks) and all(not r['blocked'] for r in rays) else 'FAIL','fbx_readback':checks,'display_clearance':rays,'not_verified':['UE import and runtime lighting','packaged game images','physical input','photoreal target acceptance'],'camera_m':list(camera),'triangle_budget':500000,'triangles':sum(c['triangles'] for c in checks)}
if report['triangles']>report['triangle_budget']:report['status']='FAIL'
# Read GLB independently, including total triangles and restored PBR texture nodes.
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
glb=OUT/'STAR_CockpitV3.glb';bpy.ops.import_scene.gltf(filepath=str(glb));objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
for o in objects:o.data.calc_loop_triangles()
triangles=sum(len(o.data.loop_triangles) for o in objects)
report['glb_readback']={'sha256':hashlib.sha256(glb.read_bytes()).hexdigest(),'mesh_objects':len(objects),'triangles':triangles,'triangle_count_match':triangles==report['triangles'],'uv_present':all(bool(o.data.uv_layers) for o in objects)}
if not report['glb_readback']['triangle_count_match'] or not report['glb_readback']['uv_present']:report['status']='FAIL'
# Source provenance is retained verbatim, without interpreting photographed lighting as albedo.
source=CONTEXT/'Data/cockpit_v3_sources.json'
if source.exists():report['retained_source_provenance']=json.loads(source.read_text(encoding='utf-8'))
diagnostic={c['part']:{'fbx_sha256':c['sha256'],**c['fbx_vector_payload']['basis_diagnostic']} for c in checks}
(OUT/'tangent_diagnostic_v04.json').write_text(json.dumps(diagnostic,indent=2),encoding='utf-8')
if any(d['nondegenerate_affected_triangles'] for d in diagnostic.values()):report['status']='FAIL'
report['tangent_quality_diagnostic']='tangent_diagnostic_v04.json; nonorthogonal original export bases occur on zero-area faces only'
report['ue_import_requirements']={'import_normals':True,'remove_degenerates':True,'recompute_tangents':'MikkTSpace'}
(ART/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
(OUT/'validation_v04.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2),flush=True)
if report['status']=='FAIL':raise RuntimeError('Cockpit asset validation failed')
