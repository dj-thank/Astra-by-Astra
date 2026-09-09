"""Independent serialized FBX / GLB checks, headless Blender, no UE claims."""
import hashlib
import json
import math
import struct
from pathlib import Path

import bmesh
import bpy

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'Content/Star/Art/CockpitControlsV4'


def fbx_tangents(path):
    from io_scene_fbx import parse_fbx
    tree,version=parse_fbx.parse(str(path))
    totals={'Tangents':0,'Binormals':0};invalid=0;layers=0
    def visit(node):
        nonlocal invalid,layers
        if node.id==b'LayerElementTangent':layers+=1
        label=node.id.decode('ascii',errors='ignore')
        if label in totals:
            values=node.props[0]
            totals[label]+=len(values)//3
            for j in range(0,len(values),3):
                v=values[j:j+3]
                if not all(math.isfinite(x) for x in v) or abs(sum(x*x for x in v)-1)>.002:invalid+=1
        for child in node.elems:visit(child)
    visit(tree)
    return {'fbx_version':version,'tangent_layers':layers,'vectors':totals,
            'invalid_vectors':invalid,'pass':layers==1 and totals['Tangents']>0 and totals['Binormals']>0 and invalid==0}


def glb_tangents(path):
    data=path.read_bytes()
    magic,version,length=struct.unpack_from('<4sII',data)
    assert magic==b'glTF' and version==2 and length==len(data)
    offset=12;document=None;binary=None
    while offset<len(data):
        size,kind=struct.unpack_from('<II',data,offset);offset+=8
        blob=data[offset:offset+size];offset+=size
        if kind==0x4E4F534A:document=json.loads(blob)
        elif kind==0x004E4942:binary=blob
    total=0;invalid=0;missing=[]
    for mesh in document['meshes']:
        for i,primitive in enumerate(mesh['primitives']):
            if 'TANGENT' not in primitive['attributes']:
                missing.append([mesh.get('name'),i]);continue
            accessor=document['accessors'][primitive['attributes']['TANGENT']]
            assert accessor['componentType']==5126 and accessor['type']=='VEC4'
            view=document['bufferViews'][accessor['bufferView']]
            start=view.get('byteOffset',0)+accessor.get('byteOffset',0)
            stride=view.get('byteStride',16)
            for j in range(accessor['count']):
                t=struct.unpack_from('<4f',binary,start+j*stride);total+=1
                if not all(math.isfinite(c) for c in t) or abs(sum(c*c for c in t[:3])-1)>.002 or abs(abs(t[3])-1)>1e-5:invalid+=1
    return {'tangent_vectors':total,'invalid_tangent_vectors':invalid,'missing_primitive_tangents':missing,
            'standard_gltf_y_up':True,'pass':total>0 and invalid==0 and not missing}


def measure(obj):
    me=obj.data;me.calc_loop_triangles()
    coords=[obj.matrix_world@v.co for v in me.vertices]
    lo=[min(v[i] for v in coords) for i in range(3)];hi=[max(v[i] for v in coords) for i in range(3)]
    bad=sum(not all(math.isfinite(c) for c in v) for v in coords)
    bm=bmesh.new();bm.from_mesh(me)
    # FBX/GLB can split vertices at tangent/UV/material seams. Weld only in this
    # temporary verifier mesh to test physical closure; serialized data is untouched.
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
    boundary=sum(e.is_boundary for e in bm.edges);wire=sum(e.is_wire for e in bm.edges)
    nonmanifold=sum(not e.is_manifold for e in bm.edges);bm.free()
    return {'name':obj.name,'triangles':len(me.loop_triangles),'bounds_m':{'min':lo,'max':hi},
            'nonfinite_vertices':bad,'degenerate_triangles':sum(t.area<1e-13 for t in me.loop_triangles),
            'invalid_corner_normals':sum(abs(n.vector.length-1)>1e-3 for n in me.corner_normals),
            'physical_boundary_edges':boundary,'physical_wire_edges':wire,'physical_nonmanifold_edges':nonmanifold,
            'materials':[m.name.split('.')[0] for m in me.materials],
            'export_origin_m':list(obj.matrix_world.translation),'uv_layers':[u.name for u in me.uv_layers]}


reference=json.loads((OUT/'validation.json').read_text(encoding='utf-8'))['new']
result={'evidence_gate':'LOCAL_EXPORTED_ASSET_ONLY','blender_version':bpy.app.version_string,'files':{}}
for extension in ('fbx','glb'):
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    path=OUT/('SM_ControlStick.'+extension)
    if extension=='fbx':bpy.ops.import_scene.fbx(filepath=str(path),use_custom_normals=True)
    else:bpy.ops.import_scene.gltf(filepath=str(path))
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    assert len(meshes)==1,(extension,len(meshes))
    item=measure(meshes[0]);item['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    delta=max(abs(item['bounds_m'][edge][i]-reference['bounds_m'][edge][i]) for edge in ('min','max') for i in range(3))
    item['bounds_max_roundtrip_delta_m']=delta
    item['checks']={'triangle_count_preserved':item['triangles']==reference['triangles'],
                    'bounds_preserved_under_1_micron':delta<1e-6,
                    'origin_preserved':all(abs(c)<1e-7 for c in item['export_origin_m']),
                    'finite_vertices':item['nonfinite_vertices']==0,
                    'nondegenerate_triangles':item['degenerate_triangles']==0,
                    'normals_valid':item['invalid_corner_normals']==0,
                    'physically_closed':item['physical_boundary_edges']==item['physical_wire_edges']==item['physical_nonmanifold_edges']==0,
                    'material_slots_preserved':set(item['materials'])==set(reference['materials'])}
    item['serialized_tangents']=glb_tangents(path) if extension=='glb' else fbx_tangents(path)
    item['checks']['serialized_tangents_present_and_valid']=item['serialized_tangents']['pass']
    result['files'][extension]=item
result['pass']=all(all(item['checks'].values()) for item in result['files'].values())
(OUT/'export_validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
manifest=json.loads((OUT/'manifest.json').read_text(encoding='utf-8'))
manifest['artifact_sha256']={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest()
    for p in OUT.rglob('*') if p.is_file() and p.name!='manifest.json' and not p.name.endswith('.blend1')}
manifest['export_validation_pass']=result['pass']
manifest['verifier_script_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
assert result['pass'],'Serialized asset roundtrip failed; see export_validation.json'
