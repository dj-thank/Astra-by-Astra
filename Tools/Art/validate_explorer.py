"""Blender local artifact QA, run in a new background process after generation."""
import json
import math
from pathlib import Path
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

root=Path(__file__).resolve().parents[2]
art=root/"Art/Explorer/V2"
report={"scope":"Blender artifact geometry, sockets and forward cockpit visibility; not UE game validation"}
bpy.ops.wm.open_mainfile(filepath=str(art/"STAR_Explorer_V2.blend"))
manifest=json.loads((art/"art_manifest.json").read_text(encoding="utf-8"))
for spec in manifest["model_parts"]:
    ob=bpy.data.objects[spec["name"]]
    spec["rotation_euler_deg"]=[math.degrees(v) for v in ob.rotation_euler]
    spec["scale"]=list(ob.scale)
    spec["geometry_rotation_and_scale_baked"]=all(abs(v)<1e-6 for v in ob.rotation_euler) and all(abs(v-1)<1e-6 for v in ob.scale)
manifest["part_file_base"]="Content/Star/Art/ExplorerV2"
for name,spec in manifest["sockets"].items():
    if not name.startswith("SOCKET_Display"): continue
    spec["center_m"]=spec["location_m"]
    spec["width_m"],spec["height_m"]=spec["size_m"]
    spec["basis"]={"normal":[-1,0,0],"right":[0,-1,0],"up":[0,0,1]}
    spec["widget_rotation_ue_deg_if_local_normal_positive_x"]={"pitch":0,"yaw":180,"roll":0}
    spec["surface_offset_m"]=.002
def world_bounds(o):
    bounds=[o.matrix_world@Vector(v) for v in o.bound_box]
    lo=Vector(tuple(min(v[i] for v in bounds) for i in range(3)))
    hi=Vector(tuple(max(v[i] for v in bounds) for i in range(3)))
    return lo,hi
manifest["collision_proxies"]=[]
for ob in bpy.data.objects:
    if not ob.name.startswith("UCX_"):continue
    lo,hi=world_bounds(ob)
    manifest["collision_proxies"].append({"name":ob.name,"location_m":list(ob.location),
        "bounds_center_m":list((lo+hi)/2),"bounds_size_m":list(hi-lo),
        "shape":"convex_hull" if "HullCeramic" in ob.name else "box"})
engine_proxy_matches=[]
for side in ("Port","Starboard"):
    a,b=world_bounds(bpy.data.objects["SM_Engine"+side])
    c,d=world_bounds(bpy.data.objects["UCX_SM_Engine"+side+"_00"])
    engine_proxy_matches.append((a-c).length<1e-4 and (b-d).length<1e-4)
report["engine_proxy_bounds_match"]=all(engine_proxy_matches)
report["collision_proxy_count"]=len(manifest["collision_proxies"])
(art/"art_manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
deps=bpy.context.evaluated_depsgraph_get()
mesh_names=[p["name"] for p in manifest["model_parts"]]
trees=[]
for name in mesh_names:
    if name=="SM_CanopyGlass": continue
    ob=bpy.data.objects[name]
    trees.append((ob,BVHTree.FromObject(ob,deps)))
eye=Vector(manifest["sockets"]["SOCKET_CockpitCamera"]["location_m"])
rays=[]
for yaw in (-12,-6,0,6,12):
    for pitch in (-6,0,6):
        d=Vector((1,math.tan(math.radians(yaw)),math.tan(math.radians(pitch)))).normalized()
        hits=[]
        for ob,tree in trees:
            inv=ob.matrix_world.inverted()
            co,n,ind,dist=tree.ray_cast(inv@eye,inv.to_3x3()@d,50)
            if co is not None: hits.append({"mesh":ob.name,"distance_m":dist})
        rays.append({"yaw_degrees":yaw,"pitch_degrees":pitch,"opaque_hits":sorted(hits,key=lambda h:h["distance_m"])})
report["cockpit_forward_rays"]=rays
report["cockpit_forward_cone_clear"]=not any(r["opaque_hits"] for r in rays)
report["triangle_count"]=manifest["triangle_count"]
report["triangle_budget_pass"]=150000 <= manifest["triangle_count"] <= 1200000
report["zero_area_faces"]=manifest["qa"]["zero_area_faces"]
report["wire_edges"]=manifest["qa"]["wire_edges"]
report["all_model_parts_have_uv"]=all(p["uv_layers"] for p in manifest["model_parts"])
report["all_model_parts_exist"]=all(bpy.data.objects.get(n) for n in mesh_names)
report["all_part_rotations_and_scales_baked"]=all(p["geometry_rotation_and_scale_baked"] for p in manifest["model_parts"])
report["bounds_m"]=manifest["bounds_m"]
report["articulated_pivots_match"]=all((bpy.data.objects[n].location-Vector(v)).length<1e-5 for n,v in manifest["gear"]["pivots_m"].items())
report["landing_sole_z_matches"]=all(abs(min((bpy.data.objects[n].matrix_world@Vector(c)).z for c in bpy.data.objects[n].bound_box)+4)<1e-4 for n in manifest["gear"]["pivots_m"])
source_bounds=manifest["bounds_m"]

# Import exported FBX back into a blank scene; the FBX metadata conversion must
# preserve the meters and axes without an undocumented second rotation.
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.unit_settings.system="METRIC"
bpy.context.scene.unit_settings.scale_length=1
bpy.ops.import_scene.fbx(filepath=str(root/manifest["fbx"]),use_manual_orientation=False)
lo=Vector((1e10,1e10,1e10)); hi=-lo
for ob in bpy.context.scene.objects:
    if ob.type!="MESH" or not ob.name.startswith("SM_"): continue
    for c in ob.bound_box:
        p=ob.matrix_world@Vector(c)
        for k in range(3): lo[k]=min(lo[k],p[k]);hi[k]=max(hi[k],p[k])
report["fbx_roundtrip_bounds_m"]={"min":list(lo),"max":list(hi),"size":list(hi-lo)}
report["fbx_roundtrip_dimensions_match"]=all(abs((hi-lo)[k]-source_bounds["size"][k])<.001 for k in range(3))
report["fbx_roundtrip_forward_axis_match"]=all(abs(lo[k]-source_bounds["min"][k])<.001 and abs(hi[k]-source_bounds["max"][k])<.001 for k in range(3))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(root/manifest["glb"]))
lo=Vector((1e10,1e10,1e10)); hi=-lo
glb_triangles=0
for ob in bpy.context.scene.objects:
    if ob.type!="MESH": continue
    ob.data.calc_loop_triangles();glb_triangles+=len(ob.data.loop_triangles)
    for c in ob.bound_box:
        p=ob.matrix_world@Vector(c)
        for k in range(3): lo[k]=min(lo[k],p[k]);hi[k]=max(hi[k],p[k])
report["glb_roundtrip_bounds_m"]={"min":list(lo),"max":list(hi),"size":list(hi-lo)}
report["glb_roundtrip_dimensions_match"]=all(abs((hi-lo)[k]-source_bounds["size"][k])<.001 for k in range(3))
report["glb_roundtrip_forward_axis_match"]=all(abs(lo[k]-source_bounds["min"][k])<.001 and abs(hi[k]-source_bounds["max"][k])<.001 for k in range(3))
report["glb_roundtrip_triangles"]=glb_triangles
report["glb_roundtrip_triangle_count_match"]=glb_triangles==manifest["triangle_count"]
report["pass"]=all(report[k] for k in ("cockpit_forward_cone_clear","triangle_budget_pass","all_model_parts_have_uv","all_model_parts_exist","all_part_rotations_and_scales_baked","articulated_pivots_match","landing_sole_z_matches","engine_proxy_bounds_match","fbx_roundtrip_dimensions_match","fbx_roundtrip_forward_axis_match","glb_roundtrip_dimensions_match","glb_roundtrip_forward_axis_match","glb_roundtrip_triangle_count_match")) and report["zero_area_faces"]==0 and report["wire_edges"]==0
(art/"validation.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(report,ensure_ascii=False,indent=2))
