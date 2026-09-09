from pathlib import Path
import bpy,json,math,hashlib
ROOT=Path(__file__).resolve().parents[3];p=ROOT/'Data/cockpit_v3_side_details.json';r=json.loads(p.read_text(encoding='utf-8'))
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
fbx=ROOT/'Content/Star/Art/CockpitV3'/r['fbx'];assert hashlib.sha256(fbx.read_bytes()).hexdigest()==r['sha256']
bpy.ops.import_scene.fbx(filepath=str(fbx),use_custom_normals=True)
objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];vs=[o.matrix_world@v.co for o in objects for v in o.data.vertices]
triangles=0
for o in objects:o.data.calc_loop_triangles();triangles+=len(o.data.loop_triangles)
b={'min':[min(v[i] for v in vs) for i in range(3)],'max':[max(v[i] for v in vs) for i in range(3)]}
checks={'mesh_count':len(objects),'triangles':triangles,'bounds_m':b,'finite':all(math.isfinite(c) for v in vs for c in v),'uv_present':all(bool(o.data.uv_layers) for o in objects),'triangle_match':triangles==r['triangles'],'bounds_match':all(abs(b[k][i]-r['bounds_m'][k][i])<.0001 for k in b for i in range(3))}
assert checks['finite'] and checks['uv_present'] and checks['triangle_match'] and checks['bounds_match'] and len(objects)==1
r['fbx_readback']=checks;p.write_text(json.dumps(r,indent=2),encoding='utf-8');print(json.dumps(checks),flush=True)
