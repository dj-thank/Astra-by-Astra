"""Reproducible original STAR Aster-24 explorer. Blender 5.1, meters, X forward.

blender --background --factory-startup --python Tools/Art/build_explorer.py -- --render
All objects, procedural microdetail and graphics are original authored geometry.
No shared Blender session, downloaded mesh, or scientific observation is used.
"""
import argparse
import json
import math
import os
from pathlib import Path
import random
import sys
import time

import bpy
import bmesh
import numpy as np
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "Art/Explorer/V2"
EXPORT = ROOT / "Content/Star/Art/ExplorerV2"
PREVIEW = ROOT / "outputs/explorer-v2"
for d in (ART, EXPORT / "Textures", EXPORT / "Parts", PREVIEW):
    d.mkdir(parents=True, exist_ok=True)
args = argparse.ArgumentParser()
args.add_argument("--render", action="store_true")
args.add_argument("--draft", action="store_true")
args.add_argument("--preview-only", action="store_true")
args.add_argument("--samples", type=int, default=32)
opts = args.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
random.seed(2409)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 8 if opts.draft else opts.samples
scene.cycles.use_denoising = True
scene.render.threads_mode = "FIXED"
scene.render.threads = 6
scene.world.use_nodes = True
scene.world.node_tree.nodes["Background"].inputs["Color"].default_value = (.05,.068,.095,1)
scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = .15
scene.view_settings.view_transform = "AgX"
scene.render.image_settings.file_format = "PNG"
scene.render.film_transparent = False

parts = {}
group = "SM_HullCeramic"
mat_specs = {}


def material(name, color, metal=0.0, rough=0.4, emission=0.0, glass=False):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Metallic"].default_value = metal
    p.inputs["Roughness"].default_value = rough
    if glass:
        p.inputs["Transmission Weight"].default_value = 1.0
        p.inputs["IOR"].default_value = 1.46
        p.inputs["Roughness"].default_value = 0.012
        p.inputs["Specular IOR Level"].default_value = .15
    if emission:
        p.inputs["Emission Color"].default_value = (*color, 1)
        p.inputs["Emission Strength"].default_value = emission
    mat_specs[name] = {"base_color_linear": list(color), "metallic": metal,
                       "roughness": rough, "emission_strength": emission,
                       "transmission": 1.0 if glass else 0,
                       "original_procedural_detail": True}
    return m


CERAMIC = material("M_CeramicIvory", (0.67, .71, .70), .12, .42)
CERAMIC_WARM = material("M_CeramicWarm", (.64,.65,.60), .13, .45)
CERAMIC_COLD = material("M_CeramicCold", (.60,.66,.69), .13, .36)
CERAMIC2 = material("M_CeramicCool", (.38, .46, .48), .4, .36)
FRAME = material("M_GraphiteStructure", (.045, .065, .076), .8, .39)
TITAN = material("M_Titanium", (.22, .26, .29), .93, .3)
SILVER = material("M_MachinedAlloy", (.49, .54, .57), .97, .23)
AMBER = material("M_MutedAmber", (.55, .21, .032), .45, .33)
BLACK = material("M_HeatShield", (.009, .014, .021), .36, .6)
RUBBER = material("M_CockpitSoftTouch", (.018, .025, .03), .03, .74)
FABRIC = material("M_SeatWoven", (.10, .145, .16), .05, .87)
GLASS = material("M_CanopyGlass", (.60, .73, .79), 0, .012, glass=True)
mat_specs["M_CanopyGlass"]["specular_ior_level"] = .15
mat_specs["M_CanopyGlass"]["ior"] = 1.46
SCREEN = material("M_DisplayBlack", (.003, .012, .017), .12, .24)
CYAN = material("M_PhosphorIce", (.14, .56, .65), .1, .3, 1.5)
WHITE = material("M_LabelWhite", (.76, .83, .83), .05, .55)
LIGHT = material("M_NavigationWhite", (.68, .9, 1), 0, .25, 4)
RED = material("M_NavigationRed", (.7, .025, .012), 0, .3, 3)
GREEN = material("M_NavigationGreen", (.025, .54, .2), 0, .3, 3)
GOLD = material("M_ThermalFoil", (.48, .30, .065), .85, .42)


def micro_textures():
    """Small exported PBR maps. Synthetic wear; linear scalar/normal and sRGB base."""
    rng = np.random.default_rng(2409)
    n = 1024
    noise = rng.normal(0, 1, (n, n)).astype(np.float32)
    wave = np.sin(np.arange(n, dtype=np.float32)[:, None] * .41)
    rough = np.clip(.50 + noise*.026 + wave*.007, 0, 1)
    height = noise*.012
    dx, dy = np.gradient(height)
    normal = np.dstack((.5-dx, .5-dy, np.ones((n,n)))).astype(np.float32)
    diffuse = np.repeat((.94 + noise[:, :, None]*.008), 3, axis=2)
    output = {}
    for key, arr, space in (("CeramicMicro_BaseColor", diffuse, "sRGB"),
                            ("CeramicMicro_Roughness", np.repeat(rough[:,:,None],3,axis=2), "Non-Color"),
                            ("CeramicMicro_Normal", normal, "Non-Color")):
        im = bpy.data.images.new(key, width=n, height=n, alpha=False)
        im.colorspace_settings.name = space
        rgba = np.ones((n,n,4), dtype=np.float32)
        rgba[:,:,:3] = arr
        im.pixels.foreach_set(rgba.ravel())
        im.filepath_raw = str(EXPORT / "Textures" / (key + ".png"))
        im.file_format = "PNG"
        im.save()
        output[key] = im
    for m in (CERAMIC, CERAMIC_WARM, CERAMIC_COLD, CERAMIC2, TITAN, SILVER, FRAME):
        nt = m.node_tree
        p = nt.nodes.get("Principled BSDF")
        uv = nt.nodes.new("ShaderNodeTexCoord")
        mapping = nt.nodes.new("ShaderNodeVectorMath")
        mapping.operation = "SCALE"
        mapping.inputs[3].default_value = 6
        nt.links.new(uv.outputs["UV"], mapping.inputs[0])
        normalnode = nt.nodes.new("ShaderNodeTexImage")
        normalnode.image = output["CeramicMicro_Normal"]
        nt.links.new(mapping.outputs[0], normalnode.inputs["Vector"])
        normalmap = nt.nodes.new("ShaderNodeNormalMap")
        normalmap.inputs["Strength"].default_value = .22
        nt.links.new(normalnode.outputs["Color"], normalmap.inputs["Color"])
        nt.links.new(normalmap.outputs["Normal"], p.inputs["Normal"])
        # Roughness map is explicitly calibrated to each material's base value.
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = output["CeramicMicro_Roughness"]
        nt.links.new(mapping.outputs[0], tex.inputs["Vector"])
        mul = nt.nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = mat_specs[m.name]["roughness"] * 2
        nt.links.new(tex.outputs["Color"], mul.inputs[0])
        nt.links.new(mul.outputs[0], p.inputs["Roughness"])
        mat_specs[m.name]["normal_texture"] = "Textures/CeramicMicro_Normal.png"
        mat_specs[m.name]["roughness_texture"] = "Textures/CeramicMicro_Roughness.png"
        mat_specs[m.name]["roughness_texture_multiplier"] = mat_specs[m.name]["roughness"]*2
        mat_specs[m.name]["uv_repeat"] = 6
    # Woven cloth has its own directional micro-normal at first-person scale.
    yy,xx=np.mgrid[0:n,0:n]
    nx=.12*np.sin(xx*math.pi/4)*(.65+.35*np.cos(yy*math.pi/4))
    ny=.12*np.sin(yy*math.pi/4)*(.65+.35*np.cos(xx*math.pi/4))
    rgba=np.ones((n,n,4),dtype=np.float32)
    rgba[:,:,0]=.5+nx;rgba[:,:,1]=.5+ny
    rgba[:,:,2]=np.sqrt(np.maximum(.0,1-nx*nx*4-ny*ny*4))*.5+.5
    im=bpy.data.images.new("SeatWeave_Normal",width=n,height=n,alpha=False)
    im.colorspace_settings.name="Non-Color"
    im.pixels.foreach_set(rgba.ravel())
    im.filepath_raw=str(EXPORT/"Textures/SeatWeave_Normal.png")
    im.file_format="PNG";im.save()
    nt=FABRIC.node_tree
    uv=nt.nodes.new("ShaderNodeTexCoord")
    tex=nt.nodes.new("ShaderNodeTexImage");tex.image=im
    nt.links.new(uv.outputs["UV"],tex.inputs["Vector"])
    nm=nt.nodes.new("ShaderNodeNormalMap");nm.inputs["Strength"].default_value=.5
    nt.links.new(tex.outputs["Color"],nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"],nt.nodes.get("Principled BSDF").inputs["Normal"])
    mat_specs[FABRIC.name]["normal_texture"]="Textures/SeatWeave_Normal.png"
    mat_specs[FABRIC.name]["normal_strength"]=.5
    mat_specs[FABRIC.name]["uv_repeat"]=1
    # Baked PBR paint at each armor panel's own UVs: sheltered faces remain clean,
    # handling grime gathers near seams, and only a tiny edge fraction is chipped.
    u=(xx+.5)/n;v=(yy+.5)/n
    edge=np.minimum.reduce([u,1-u,v,1-v])
    cloud=(np.sin(u*18+np.cos(v*11)) + np.sin(v*27+u*7))*.5
    grime=np.exp(-edge/.017)*(.045+.016*np.sin(v*83+u*19))
    chips=(edge<(.0011+.00065*(noise>.45))) & (noise>.20)
    factor=np.clip(.981+cloud*.012+noise*.0035-grime,.78,1.025)
    for m in (CERAMIC,CERAMIC_WARM,CERAMIC_COLD):
        tint=np.array(mat_specs[m.name]["base_color_linear"],dtype=np.float32)
        rgb=factor[:,:,None]*tint
        rgb[chips]=(.24,.275,.29)
        rough=np.clip(mat_specs[m.name]["roughness"]+grime*.9+cloud*.015+noise*.005,.2,.75)
        rough[chips]=.29
        metal=np.full((n,n),mat_specs[m.name]["metallic"],dtype=np.float32);metal[chips]=.85
        nt=m.node_tree;p=nt.nodes.get("Principled BSDF")
        coord=nt.nodes.new("ShaderNodeTexCoord")
        for role,arr,space,socket in (("BaseColor",rgb,"sRGB","Base Color"),
                                     ("Roughness",np.repeat(rough[:,:,None],3,axis=2),"Non-Color","Roughness"),
                                     ("Metallic",np.repeat(metal[:,:,None],3,axis=2),"Non-Color","Metallic")):
            im=bpy.data.images.new("Paint_"+m.name+"_"+role,width=n,height=n,alpha=False)
            im.colorspace_settings.name=space
            rgba=np.ones((n,n,4),dtype=np.float32);rgba[:,:,:3]=arr
            im.pixels.foreach_set(rgba.ravel())
            im.filepath_raw=str(EXPORT/"Textures"/(im.name+".png"));im.file_format="PNG";im.save()
            tex=nt.nodes.new("ShaderNodeTexImage");tex.image=im
            nt.links.new(coord.outputs["UV"],tex.inputs["Vector"])
            nt.links.new(tex.outputs["Color"],p.inputs[socket])
            key={"BaseColor":"base_color_texture","Roughness":"roughness_texture","Metallic":"metallic_texture"}[role]
            mat_specs[m.name][key]="Textures/"+im.name+".png"
        mat_specs[m.name]["roughness_texture_multiplier"]=1
        mat_specs[m.name]["base_color_texture_tint_baked"]=True
        mat_specs[m.name]["paint_uv_repeat"]=1
        mat_specs[m.name]["normal_uv_repeat"]=6


micro_textures()


def register(o, name, mat, smooth=False, bevel=0):
    o.name = name
    if mat:
        o.data.materials.append(mat)
    if o.type == "MESH":
        if not o.data.uv_layers:
            uv = o.data.uv_layers.new(name="UVMap")
            for p in o.data.polygons:
                normal = p.normal
                k = max(range(3), key=lambda i: abs(normal[i]))
                ij = [i for i in range(3) if i != k]
                for li in p.loop_indices:
                    v = o.data.vertices[o.data.loops[li].vertex_index].co
                    uv.data[li].uv = (v[ij[0]], v[ij[1]])
        if smooth:
            for p in o.data.polygons:
                p.use_smooth = True
        if bevel:
            mod = o.modifiers.new("Machined edge radius", "BEVEL")
            mod.width = bevel
            mod.segments = 3
            mod.limit_method = "ANGLE"
            mod.angle_limit = .45
            if mat==FABRIC:
                mod.segments=6
                for p in o.data.polygons: p.use_smooth=True
                weighted=o.modifiers.new("Upholstery weighted normals","WEIGHTED_NORMAL")
                weighted.keep_sharp=True
    parts.setdefault(group, []).append(o)
    return o


def mesh(name, verts, faces, mat, bevel=0, smooth=False):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return register(o, name, mat, smooth, bevel)


def box(name, loc, size, mat, bevel=.035):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return register(o, name, mat, False, bevel)


def cylinder(name, a, b, radius, mat, vertices=32, radius2=None):
    a, b = Vector(a), Vector(b)
    d = b-a
    bpy.ops.mesh.primitive_cone_add(vertices=vertices, radius1=radius,
                                   radius2=radius if radius2 is None else radius2,
                                   depth=d.length, location=(a+b)/2)
    o = bpy.context.object
    o.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    return register(o, name, mat, True, .009 if radius > .03 else 0)


def beam(name, a, b, width, mat, height=None):
    a,b = Vector(a),Vector(b)
    o = box(name, (a+b)/2, (width, height or width, (b-a).length), mat, min(width*.12,.035))
    o.rotation_euler = (b-a).to_track_quat("Z","Y").to_euler()
    return o


def sphere(name, loc, scale, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, location=loc)
    o=bpy.context.object
    o.scale=scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return register(o,name,mat,True)


def ring(name, loc, major, minor, mat, axis=(1,0,0), segments=64):
    bpy.ops.mesh.primitive_torus_add(major_segments=segments, minor_segments=12,
                                   location=loc, major_radius=major, minor_radius=minor)
    o=bpy.context.object
    o.rotation_euler=Vector(axis).to_track_quat("Z","Y").to_euler()
    return register(o,name,mat,True)


def plate(name, points, mat, thickness=.04, bevel=.012):
    points=[Vector(p) for p in points]
    n=(points[1]-points[0]).cross(points[2]-points[0]).normalized()
    verts=[tuple(p) for p in points]+[tuple(p-n*thickness) for p in points]
    q=len(points)
    faces=[tuple(range(q)),tuple(range(q*2-1,q-1,-1))]
    faces += [(i,(i+1)%q,(i+1)%q+q,i+q) for i in range(q)]
    return mesh(name,verts,faces,mat,bevel)


def panel_inset(points, margin=.004):
    """An 8 mm physical seam, independent of the width of a hull panel."""
    p=[Vector(v) for v in points]
    t=min(.16,margin/min((p[1]-p[0]).length,(p[2]-p[3]).length))
    s=min(.16,margin/min((p[3]-p[0]).length,(p[2]-p[1]).length))
    def at(u,v): return p[0]*(1-u)*(1-v)+p[1]*u*(1-v)+p[2]*u*v+p[3]*(1-u)*v
    return [at(t,s),at(1-t,s),at(1-t,1-s),at(t,1-s)]


def tube(name, points, radius, mat):
    # Actual polygonal plumbing, not render-only curve geometry.
    sides=12
    vv=[]
    ff=[]
    for j,p in enumerate(points):
        t=Vector(points[min(j+1,len(points)-1)])-Vector(points[max(0,j-1)])
        q=t.to_track_quat("Z","Y")
        for k in range(sides):
            a=2*math.pi*k/sides
            vv.append(Vector(p)+q@Vector((radius*math.cos(a),radius*math.sin(a),0)))
    for j in range(len(points)-1):
        for k in range(sides):
            ff.append((j*sides+k,j*sides+(k+1)%sides,(j+1)*sides+(k+1)%sides,(j+1)*sides+k))
    ff += [tuple(range(sides-1,-1,-1)),tuple((len(points)-1)*sides+k for k in range(sides))]
    return mesh(name,vv,ff,mat,smooth=True)


fontpath=Path("C:/Windows/Fonts/bahnschrift.ttf")
font=bpy.data.fonts.load(str(fontpath)) if fontpath.exists() else None


def label(name, txt, pos, size, mat=WHITE, rotation=(0,0,0), align="LEFT"):
    cu=bpy.data.curves.new(name,"FONT")
    cu.body=txt
    cu.size=size
    cu.extrude=.0007
    cu.bevel_depth=0
    cu.resolution_u=3
    cu.align_x=align
    if font: cu.font=font
    o=bpy.data.objects.new(name,cu)
    bpy.context.collection.objects.link(o)
    o.location=pos
    o.rotation_euler=rotation
    cu.materials.append(mat)
    parts.setdefault(group,[]).append(o)
    return o


def nozzle(name, base, axis, radius, length):
    """Open hollow nozzle with lip and dark throat, no solid cone blocking exhaust."""
    base=Vector(base)
    axis=Vector(axis).normalized()
    q=axis.to_track_quat("Z","Y")
    sections=[(0,.42),(length*.26,.47),(length*.7,.81),(length,1),
              (length,.92),(length*.68,.73),(length*.27,.39)]
    vv=[]
    for z,r in sections:
        for k in range(64):
            a=2*math.pi*k/64
            vv.append(base+q@Vector((radius*r*math.cos(a),radius*r*math.sin(a),z)))
    ff=[]
    for j in range(len(sections)-1):
        for k in range(64): ff.append((j*64+k,j*64+(k+1)%64,(j+1)*64+(k+1)%64,(j+1)*64+k))
    ff.append(tuple((len(sections)-1)*64+k for k in range(64)))
    ff.append(tuple(range(63,-1,-1)))
    o=mesh(name,vv,ff,TITAN,smooth=True)
    o.data.materials.append(BLACK)
    for p in o.data.polygons:
        if p.index >=64*4: p.material_index=1
    ring(name+"_Lip",base+axis*length,radius*.97,radius*.045,SILVER,axis)
    cylinder(name+"_Throat",base+axis*length*.27,base+axis*length*.285,radius*.365,BLACK)
    return o


def bolt(loc, normal=(0,0,1), radius=.034):
    p=Vector(loc); n=Vector(normal)
    cylinder("Captive fastener",p,p+n*.018,radius,TITAN,6)
    cylinder("Fastener socket",p+n*.018,p+n*.0188,radius*.42,BLACK,6)


# V2 is newly constructed geometry driven by the supplied design reference.
exec(compile((ROOT / "Tools/Art/explorer_v2_geometry.py").read_text(encoding="utf-8"), "explorer_v2_geometry.py", "exec"), globals())

# Convert/apply modifiers then consolidate related details, retaining part pivots.
print("STAR: geometry authored, applying machined edges", flush=True)
pivots=dict(gear_pivots)
pivots.update({"SM_ControlStick":stick_pivot,"SM_Throttle":throttle_pivot,
               "SM_EnginePort":(-9.45,-4.7,0),"SM_EngineStarboard":(-9.45,4.7,0)})
pivots.update(engine_pivots)
flush_fasteners()
final=[]
for name, objects in parts.items():
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects: o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.join()
    o=bpy.context.object
    o.name=name
    o["recon_part"]=name
    scene.cursor.location=pivots.get(name,(0,0,0))
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    # Apply scale/rotation but retain documented pivot position.
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    # Bevel-clamped corners and text cap seams can contain numerical slivers.
    # Weld only within one micrometer; preserve authored panel gaps (centimeters).
    bm=bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000001)
    bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.000001)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(o.data)
    bm.free()
    o.data.update()
    final.append(o)
    print("STAR part",name,len(o.data.polygons),flush=True)

sockets={"SOCKET_CockpitCamera":{"location_m":[6,0,1.3],"forward":[1,0,0],"up":[0,0,1]},
         "SOCKET_ChaseCamera":{"location_m":[-24,0,9],"look_at_m":[2,0,0]},
         "SOCKET_LandingFootNose":{"location_m":[7,0,-4]},
         "SOCKET_LandingFootPort":{"location_m":[-5,-4.8,-4]},
         "SOCKET_LandingFootStarboard":{"location_m":[-5,4.8,-4]},
         "SOCKET_Origin":{"location_m":[0,0,0]}}
sockets.update(screen_sockets)
for name,spec in sockets.items():
    o=bpy.data.objects.new(name,None)
    o.empty_display_type="ARROWS"
    o.empty_display_size=.25
    o.location=spec["location_m"]
    bpy.context.collection.objects.link(o)

# A compact explicit collision proxy, independent of visual panel/pipe detail.
group="Collision"
collision_points=[tuple(v) for rr in rv for v in rr]
collision_points += [(3.44,-1.66,2.10),(3.44,1.66,2.10),(5.44,-1.57,2.22),(5.44,1.57,2.22),(10.67,-.86,.30),(10.67,.86,.30)]
col=mesh("UCX_SM_HullCeramic_00",collision_points,[],None)
bm=bmesh.new();bm.from_mesh(col.data)
bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context="VERTS")
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
bm.to_mesh(col.data);bm.free();col.data.update()
col.hide_render=True
col.display_type="WIRE"
for s,side in ((-1,"Port"),(1,"Starboard")):
    engine=bpy.data.objects["SM_Engine"+side]
    bounds=[engine.matrix_world@Vector(v) for v in engine.bound_box]
    low=Vector(tuple(min(v[i] for v in bounds) for i in range(3)))
    high=Vector(tuple(max(v[i] for v in bounds) for i in range(3)))
    o=box("UCX_SM_Engine"+side+"_00",(low+high)/2,high-low,None,0)
    o.hide_render=True
    o.display_type="WIRE"
scene.cursor.location=(0,0,0)


def evaluate():
    stats=[]
    minv=Vector((1e10,1e10,1e10)); maxv=-minv
    total=0
    for o in final:
        me=o.data
        me.calc_loop_triangles()
        tri=len(me.loop_triangles); total+=tri
        bm=bmesh.new(); bm.from_mesh(me)
        deg=sum(f.calc_area()<1e-12 for f in bm.faces)
        boundary=sum(e.is_boundary for e in bm.edges)
        wire=sum(e.is_wire for e in bm.edges)
        nonmanifold=sum(not e.is_manifold for e in bm.edges)
        bm.free()
        for c in o.bound_box:
            v=o.matrix_world@Vector(c)
            for k in range(3): minv[k]=min(minv[k],v[k]); maxv[k]=max(maxv[k],v[k])
        stats.append({"name":o.name,"triangles":tri,"vertices":len(me.vertices),
                      "location_m":list(o.location),"dimensions_m":list(o.dimensions),
                      "rotation_euler_deg":[math.degrees(v) for v in o.rotation_euler],
                      "scale":list(o.scale),"geometry_rotation_and_scale_baked":True,
                      "uv_layers":[u.name for u in me.uv_layers],"zero_area_faces":deg,
                      "boundary_edges":boundary,"wire_edges":wire,"nonmanifold_edges":nonmanifold,
                      "materials":[m.name for m in me.materials],
                      "fbx":"Parts/"+o.name+".fbx"})
    return stats,total,minv,maxv


stats,total,minv,maxv=evaluate()
def collision_spec(o):
    bounds=[o.matrix_world@Vector(v) for v in o.bound_box]
    low=Vector(tuple(min(v[i] for v in bounds) for i in range(3)))
    high=Vector(tuple(max(v[i] for v in bounds) for i in range(3)))
    return {"name":o.name,"location_m":list(o.location),"bounds_center_m":list((low+high)/2),
            "bounds_size_m":list(high-low),"shape":"convex_hull" if "HullCeramic" in o.name else "box"}
manifest={"asset":"STAR ASTER-24 V2 reference-driven science explorer","version":2,
          "reference":"Art/Explorer/V2/explorer-v2-concept.png",
          "reference_type":"ImageGen generated design target, not an observation",
          "status":"shape_draft" if opts.draft else "candidate_visual_review",
          "authoring":"Original procedural Blender mesh, no third-party artwork",
          "source_date":"2026-09-06","scientific_observation":False,
          "blender_version":bpy.app.version_string,"source_units":"meters",
          "source_frame":{"forward":"+X","right":"+Y","up":"+Z"},
          "unreal_units":"centimeters","fbx_axis_forward":"X","fbx_axis_up":"Z",
          "import_uniform_scale":1,"fbx_apply_unit_scale":True,
          "fbx_scale_options":"FBX_SCALE_UNITS","glb_axis_conversion":"Blender glTF exporter standard +Y up",
          "blend":"Art/Explorer/V2/STAR_Explorer_V2.blend",
          "fbx":"Content/Star/Art/ExplorerV2/STAR_Explorer_V2.fbx",
          "part_file_base":"Content/Star/Art/ExplorerV2",
          "glb":"Content/Star/Art/ExplorerV2/STAR_Explorer_V2.glb",
          "bounds_m":{"min":list(minv),"max":list(maxv),"size":list(maxv-minv)},
          "mesh_count":len(stats),"triangle_count":total,"model_parts":stats,
          "collision_proxies":[collision_spec(o) for o in parts["Collision"]],
          "materials":mat_specs,"sockets":sockets,"rcs":rcs_manifest,
          "gear":{"deployed_foot_z_m":-4.0,"deployed_origin_clearance_m":4.0,
                  "feet_m":feet,"pivots_m":gear_pivots,
                  "retraction":"Rotate gear as rigid assemblies around local Y; inspect swept bounds before runtime use. No rig or animation clip is claimed."},
          "controls":{"stick_pivot_m":stick_pivot,"throttle_pivot_m":throttle_pivot},
          "qa":{"zero_area_faces":sum(p["zero_area_faces"] for p in stats),
                "wire_edges":sum(p["wire_edges"] for p in stats),
                "render_engine":"Cycles CPU","render_samples":opts.samples,
                "game_runtime_validated":False},
          "import_caveats":["Glass requires a UE translucent thin-glass material, separate from Nanite opaque parts.",
                            "Apply material scalar multipliers from manifest; arbitrary Blender node graphs are not fully carried by FBX.",
                            "Static cockpit screens are unpopulated instrument surfaces; apply live game UI at display sockets.",
                            "Assembled FBX is a scene import; for separate parts use their pivot translations from model_parts.",
                            "Blender preview is not an Unreal packaged-game or performance acceptance."]}
(ART/"art_manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
print("STAR triangles",total,"bounds",list(maxv-minv),flush=True)

if not opts.preview_only and not opts.draft:
    bpy.ops.object.select_all(action="DESELECT")
    for o in final: o.select_set(True)
    for o in parts["Collision"]: o.select_set(True)
    for name in sockets: bpy.data.objects[name].select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(EXPORT/"STAR_Explorer_V2.fbx"),use_selection=True,
                             axis_forward="X",axis_up="Z",global_scale=1,apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS",object_types={"MESH","EMPTY"},
                             bake_anim=False,path_mode="COPY",embed_textures=False,
                             use_mesh_modifiers=True,add_leaf_bones=False)
    for o in final:
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        bpy.context.view_layer.objects.active=o
        # Per-part FBX geometry is local to its pivot; manifest retains assembly placement.
        keep=o.location.copy(); o.location=(0,0,0)
        bpy.ops.export_scene.fbx(filepath=str(EXPORT/"Parts"/(o.name+".fbx")),use_selection=True,
                                 axis_forward="X",axis_up="Z",global_scale=1,apply_unit_scale=True,
                                 apply_scale_options="FBX_SCALE_UNITS",object_types={"MESH"},
                                 bake_anim=False,path_mode="RELATIVE",use_mesh_modifiers=True)
        o.location=keep
    bpy.ops.object.select_all(action="DESELECT")
    for o in final: o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(EXPORT/"STAR_Explorer_V2.glb"),export_format="GLB",
                             use_selection=True,export_yup=True,export_apply=True,
                             export_texcoords=True,export_normals=True,export_materials="EXPORT")

# Readable neutral photographic setup kept in a separate render-only collection.
stage=bpy.data.collections.new("PREVIEW_ONLY_Studio")
scene.collection.children.link(stage)


def stage_obj(o):
    for c in list(o.users_collection): c.objects.unlink(o)
    stage.objects.link(o)
    return o


def area(name,loc,power,size,color,target=(0,0,0)):
    dat=bpy.data.lights.new(name,"AREA"); dat.energy=power; dat.shape="DISK"; dat.size=size; dat.color=color
    o=bpy.data.objects.new(name,dat); stage.objects.link(o); o.location=loc
    o.visible_glossy=True
    o.visible_camera=False
    o.visible_transmission=False
    o.rotation_euler=(Vector(target)-o.location).to_track_quat("-Z","Y").to_euler()
    return o


area("Soft key",(4,-11,16),6000,12,(.81,.89,1))
area("Long rim",(-10,9,12),8500,10,(.57,.73,1))
area("Nose fill",(14,5,6),3400,8,(1,.83,.63))
area("Aft softbox",(-14,-4,6),1700,6,(.81,.92,1))
area("Ventral bounce",(1,-4,-1.2),220,6,(.45,.58,.8),(0,0,-2))
area("Ventral inspection softbox",(2,-5,-8),3600,10,(.66,.79,1),(0,0,-1))
area("Cockpit cabin bounce",(4.8,0,1.77),24,1.1,(.68,.85,1),(6.1,0,.6))
area("Low mechanical fill",(9,-10,-.7),2600,8,(.67,.79,1),(0,0,-2))
floor_mat=material("PREVIEW_Floor",(.016,.022,.03),.2,.36)
group="Preview"
floor=stage_obj(box("PREVIEW floor",(0,0,-4.055),(200,200,.05),floor_mat,0))
camd=bpy.data.cameras.new("PreviewCamera")
camera=bpy.data.objects.new("PreviewCamera",camd); stage.objects.link(camera); scene.camera=camera
camera.data.lens=48
camera.data.clip_end=2000
camera.data.clip_start=.04
shots=[("01_hero",(25,-28,19),(0,0,-.1),50,1700,1150),
       ("02_chase",(-24,-24,13),(-1,0,.1),47,1600,1100),
       ("03_front",(29,-.01,8),(1,0,-.1),48,1600,1100),
       ("04_underside",(21,-22,-13),(0,0,-1),47,1600,1100),
       ("05_cockpit",(6,0,1.3),(10,0,1.05),15,1600,1000),
       ("06_cockpit_detail",(6.63,1.05,1.56),(5.3,-.61,.81),24,1400,1100)]
# Save editable source including studio, with hero camera configured.
camera.location=shots[0][1]
camera.rotation_euler=(Vector(shots[0][2])-camera.location).to_track_quat("-Z","Y").to_euler()
for im in bpy.data.images:
    if im.name.startswith(("CeramicMicro_","SeatWeave_","Paint_")):
        im.pack()
        im.filepath="//../../../Content/Star/Art/ExplorerV2/Textures/"+im.name+".png"
bpy.ops.wm.save_as_mainfile(filepath=str(ART/"STAR_Explorer_V2.blend"),compress=True)
if opts.render:
    # Prioritize the cockpit and underside acceptance views, then exterior beauty.
    for name,loc,target,lens,w,h in ([shots[0]] if opts.draft else [shots[0],shots[4],shots[1],shots[3],shots[2],shots[5]]):
        if opts.draft:
            name="00_shape_preview";w=1152;h=780
        floor.hide_render=name=="04_underside"
        for light in stage.objects:
            if light.type=="LIGHT":
                # The studio's large luminous discs should not fill the cockpit
                # windscreen. Geometry and the exported glass material are unchanged.
                light.data.specular_factor=0 if name=="05_cockpit" else 1
                light.data.transmission_factor=0 if name=="05_cockpit" else 1
        camera.location=loc
        camera.rotation_euler=(Vector(target)-camera.location).to_track_quat("-Z","Y").to_euler()
        camera.data.lens=lens
        scene.render.resolution_x=w; scene.render.resolution_y=h; scene.render.resolution_percentage=100
        scene.render.filepath=str(PREVIEW/(name+".png"))
        print("STAR rendering",name,flush=True)
        bpy.ops.render.render(write_still=True)
    floor.hide_render=False
print("STAR BUILD COMPLETE",flush=True)
