"""Original STAR flight control replacement. Run with headless Blender 5.1, CPU4.

All geometry is in meters, +X forward, +Y right, +Z up. The exported origin is
the inherited control pivot. No UE runtime or input behavior is authored here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import shutil
import sys
import traceback
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector


PIVOT = [5.920000076293945, -0.4099999964237213, 0.5699999928474426]
ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Content/Star/Art/CockpitControlsV4'
WORK = ROOT / 'work/controls-v4'
PARTS = []
TUBE_FRAME_CHECKS = []


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record(name, value):
    (WORK / (name + '.json')).write_text(json.dumps(value, indent=2), encoding='utf-8')


def mesh(name, verts, faces, mat, smooth=True):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.update()
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(data)
    bm.free()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    for p in data.polygons:
        p.use_smooth = smooth
    PARTS.append(obj)
    return obj


def apply(obj, mod):
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def bevel(obj, width, segments=4):
    mod = obj.modifiers.new('Manufactured edge radius', 'BEVEL')
    mod.width = width
    mod.segments = segments
    apply(obj, mod)
    for p in obj.data.polygons:
        p.use_smooth = True
    mod = obj.modifiers.new('Face weighted corner normals', 'WEIGHTED_NORMAL')
    mod.keep_sharp = True
    mod.weight = 40
    apply(obj, mod)
    return obj


def box(name, loc, size, mat, edge=.001):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    PARTS.append(obj)
    return bevel(obj, edge)


def tube(name, points, radius, mat, sides=32):
    vv, ff = [], []
    frames=[]
    for j, p in enumerate(points):
        tangent = Vector(points[min(j+1, len(points)-1)]) - Vector(points[max(0, j-1)])
        tangent.normalize()
        reference=Vector((0,1,0)) if abs(tangent.y)<.95 else Vector((1,0,0))
        u=(reference-tangent*reference.dot(tangent)).normalized()
        v=tangent.cross(u).normalized()
        frames.append(u)
        for k in range(sides):
            a = math.tau*k/sides
            vv.append(Vector(p) + radius*(u*math.cos(a)+v*math.sin(a)))
    for j in range(len(points)-1):
        for k in range(sides):
            a = j*sides+k
            b = j*sides+(k+1)%sides
            ff.append((a,b,b+sides,a+sides))
    ff += [tuple(range(sides-1,-1,-1)), tuple(range((len(points)-1)*sides,len(points)*sides))]
    TUBE_FRAME_CHECKS.append({'name':name,'adjacent_frame_min_dot':min(a.dot(b) for a,b in zip(frames,frames[1:]))})
    return mesh(name,vv,ff,mat)


def lathe(name, profile, mat, center=(0,0), sides=64):
    vv, ff = [], []
    for z, radius in profile:
        for k in range(sides):
            a = math.tau*k/sides
            vv.append((center[0]+radius*math.cos(a),center[1]+radius*math.sin(a),z))
    for j in range(len(profile)-1):
        for k in range(sides):
            a = j*sides+k; b = j*sides+(k+1)%sides
            ff.append((a,b,b+sides,a+sides))
    ff += [tuple(range(sides-1,-1,-1)),tuple(range((len(profile)-1)*sides,len(profile)*sides))]
    return mesh(name,vv,ff,mat)


def cylinder(name, a, b, r, mat, sides=32, edge=.0005):
    a,b=Vector(a),Vector(b)
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides,radius=r,depth=(b-a).length,location=(a+b)/2)
    obj=bpy.context.object; obj.name=name
    obj.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler()
    obj.data.materials.append(mat); PARTS.append(obj)
    return bevel(obj,edge,3)


def screw(name, loc, axis, mat, dark, radius=.003):
    p=Vector(loc); n=Vector(axis)
    cylinder(name+' recessed ferrule',p-n*.0012,p,radius*1.35,dark,24,.00025)
    cylinder(name+' captive head',p-n*.0005,p+n*.0003,radius,mat,24,.0002)
    # Two physical shallow dark slots, each contained by the screw head.
    for angle in (0,math.pi/2):
        ob=box(name+' drive',p+n*.00038,(radius*1.36,.00055,.0003),dark,.00014)
        ob.rotation_euler=n.to_track_quat('Z','Y').to_euler()
        ob.rotation_euler.rotate_axis('Z',angle)


def material(name, values):
    mat=bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes=True
    nodes=mat.node_tree.nodes; nodes.clear()
    shader=nodes.new('ShaderNodeBsdfPrincipled')
    shader.inputs['Base Color'].default_value=(*values['base_color_linear'],1)
    shader.inputs['Metallic'].default_value=values['metallic']
    shader.inputs['Roughness'].default_value=values['roughness']
    output=nodes.new('ShaderNodeOutputMaterial')
    mat.node_tree.links.new(shader.outputs['BSDF'],output.inputs['Surface'])
    mat.diffuse_color=(*values['base_color_linear'],1)
    return mat


def bounds(obj):
    vs=[obj.matrix_world@v.co for v in obj.data.vertices]
    lo=[min(v[i] for v in vs) for i in range(3)]
    hi=[max(v[i] for v in vs) for i in range(3)]
    return {'min':lo,'max':hi,'size':[hi[i]-lo[i] for i in range(3)]}


def inspect(obj):
    me=obj.data; me.calc_loop_triangles()
    bm=bmesh.new(); bm.from_mesh(me)
    result={'name':obj.name,'vertices':len(me.vertices),'triangles':len(me.loop_triangles),
            'bounds_m':bounds(obj),'zero_area_faces':sum(p.area<1e-13 for p in me.polygons),
            'boundary_edges':sum(e.is_boundary for e in bm.edges),
            'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
            'wire_edges':sum(e.is_wire for e in bm.edges),
            'nonfinite_vertices':sum(not all(math.isfinite(c) for c in v.co) for v in me.vertices),
            'invalid_corner_normals':sum(abs(n.vector.length-1)>1e-3 for n in me.corner_normals),
            'materials':[m.name for m in me.materials],
            'transform':{'location':list(obj.location),'rotation_euler':list(obj.rotation_euler),'scale':list(obj.scale)}}
    bm.free()
    return result


def interpolate(profile, steps=3):
    result=[]
    for j in range(len(profile)-1):
        p0=profile[max(0,j-1)];p1=profile[j];p2=profile[j+1];p3=profile[min(len(profile)-1,j+2)]
        for s in range(steps):
            t=s/steps
            result.append(tuple(.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t)
                                for a,b,c,d in zip(p0,p1,p2,p3)))
    result.append(profile[-1]);return result


def build_grip(polymer, dark, titanium, alloy, amber):
    profile=[(.336,-.041,.029,.027),(.343,-.042,.044,.036),(.353,-.042,.057,.045),
             (.365,-.044,.058,.044),(.385,-.048,.053,.040),(.410,-.051,.051,.039),
             (.432,-.054,.051,.039),(.452,-.057,.055,.042),(.477,-.060,.061,.045),
             (.498,-.062,.062,.046),(.516,-.064,.060,.045),(.531,-.065,.054,.041),
             (.540,-.066,.039,.032),(.545,-.066,.019,.019),(.546,-.066,.005,.007)]
    rings=interpolate(profile,3); sides=64;vv=[];ff=[]
    for z,cx,rx,ry in rings:
        for k in range(sides):
            a=math.tau*k/sides
            # Superellipse becomes softer at the crown; asymmetric front finger valleys.
            exponent=.80 if z<.51 else .90
            ca,sa=math.cos(a),math.sin(a)
            xx=math.copysign(abs(ca)**exponent,ca)
            yy=math.copysign(abs(sa)**exponent,sa)
            fingers=sum(.0038*math.exp(-((z-h)/.0065)**2) for h in (.386,.415,.442))
            front=max(0,ca)**8
            seam=.0005*math.exp(-(sa/.045)**2)
            vv.append((cx+(rx-seam)*xx-fingers*front,(ry-seam)*yy,z))
    for j in range(len(rings)-1):
        for k in range(sides):
            a=j*sides+k;b=j*sides+(k+1)%sides;ff.append((a,b,b+sides,a+sides))
    ff += [tuple(range(sides-1,-1,-1)),tuple(range((len(rings)-1)*sides,len(rings)*sides))]
    grip=mesh('Moulded ergonomic shell with finger valleys',vv,ff,polymer)
    # Closed thin inset palm panels on either side; conform to the real shell surface.
    for side in (-1,1):
        pv=[];pf=[]; rows=16; cols=16
        for layer in (0,1):
            for j in range(rows):
                t=j/(rows-1); z=.369+t*.119
                idx=next(k for k in range(len(rings)-1) if rings[k][0]<=z<=rings[k+1][0])
                f=(z-rings[idx][0])/(rings[idx+1][0]-rings[idx][0])
                _,cx,rx,ry=tuple(a+(b-a)*f for a,b in zip(rings[idx],rings[idx+1]))
                width=.44*min(1,.48+math.sin(math.pi*t)*.70)
                for k in range(cols):
                    a=side*math.pi/2+(k/(cols-1)-.5)*width*2
                    offset=.0008 if layer else -.0006
                    ca,sa=math.cos(a),math.sin(a)
                    pv.append((cx+(rx+offset)*math.copysign(abs(ca)**.8,ca),
                               (ry+offset)*math.copysign(abs(sa)**.8,sa),z))
        size=rows*cols
        for layer in (0,1):
            for j in range(rows-1):
                for k in range(cols-1):
                    a=layer*size+j*cols+k; f=(a,a+1,a+1+cols,a+cols)
                    pf.append(f if layer else f[::-1])
        border=list(range(cols))+[j*cols+cols-1 for j in range(1,rows)]+list(range(size-2,size-cols-1,-1))+[j*cols for j in range(rows-2,0,-1)]
        for j,a in enumerate(border):
            b=border[(j+1)%len(border)];pf.append((a,b,b+size,a+size))
        mesh('Inlaid palm traction panel '+str(side),pv,pf,dark)
    # Trigger lies inside the old orange box envelope, with a recessed dark cradle.
    cradle=box('Inset scan trigger cradle',(.000,0,.474),(.026,.056,.064),dark,.009)
    cradle.rotation_euler.y=-.12
    trigger=box('Guarded scan paddle',(.010,0,.476),(.012,.040,.041),titanium,.005)
    trigger.rotation_euler.y=-.12
    box('Small amber scan identifier',(.0163,0,.481),(.0018,.026,.006),amber,.0008)
    for z in (.465,.470):
        box('Trigger tactile ribs',(.016,0,z),(.0018,.031,.0018),dark,.0007)
    cylinder('Trigger pivot pin',(.000,-.0287,.495),(.000,.0287,.495),.0031,alloy,24)
    # Thumb deck and four-way hat: low profile, recessed ring and rounded cap.
    deck=box('Thumb control inset deck',(-.086,.003,.531),(.067,.068,.025),dark,.012)
    deck.rotation_euler.y=-.10
    cylinder('Thumb hat retaining bezel',(-.087,.014,.541),(-.087,.014,.548),.019,titanium,48)
    cylinder('Thumb hat dust seal',(-.087,.014,.547),(-.087,.014,.550),.0165,polymer,48)
    cap=box('Four way concave thumb cap',(-.087,.014,.552),(.028,.028,.007),dark,.006)
    for dx,dy in ((-.009,0),(.009,0),(0,-.009),(0,.009)):
        box('Thumb directional tactile notch',(-.087+dx,.014+dy,.5555),(.0025,.0025,.0008),titanium,.0007)
    cylinder('Secondary thumb button recess',(-.087,-.022,.539),(-.087,-.022,.545),.009,dark,32)
    cylinder('Secondary thumb button',(-.087,-.022,.544),(-.087,-.022,.548),.0063,amber,32)
    for side in (-1,1):
        screw('Shell service screw '+str(side),(-.063,side*.0426,.496),(0,side,0),alloy,dark,.0025)
    lathe('Grip lower ferrule',[(.326,.026),(.329,.030),(.337,.030),(.341,.027)],titanium,(-.04,0),48)


def build_base(polymer,dark,titanium,alloy):
    lathe('Gimbal mounting flange', [(-.062,.073),(-.060,.084),(-.055,.096),(-.050,.098),(-.046,.098),(-.044,.092)],titanium)
    lathe('Flange perimeter elastomer seal',[(-.056,.094),(-.053,.0985),(-.051,.0985),(-.049,.096)],dark)
    # Six rounded convolutions replace the undifferentiated sphere. Radius never exceeds inherited volume.
    profile=[(-.047,.089),(-.042,.092),(-.035,.097),(-.028,.103),(-.020,.105),(-.014,.100),
             (-.008,.091),(-.003,.093),(.003,.096),(.008,.092),(.013,.081),(.018,.078),
             (.023,.081),(.028,.077),(.033,.065),(.038,.062),(.043,.065),(.047,.059),
             (.052,.047),(.057,.043),(.061,.045),(.065,.040),(.070,.030),(.075,.027)]
    lathe('Moulded bellows with concentric folds',interpolate(profile,2),polymer,sides=48)
    lathe('Bellows stem retaining collar',[(.070,.028),(.073,.031),(.081,.031),(.084,.027)],alloy,sides=48)
    for i in range(4):
        a=math.pi*.25+i*math.pi*.5
        screw('Mount captive screw '+str(i),(.080*math.cos(a),.080*math.sin(a),-.043),(0,0,1),alloy,dark,.004)
    points=[(0,0,.077),(.003,0,.13),(.007,0,.185),(.008,0,.215),(.004,0,.243),(-.005,0,.271),(-.018,0,.301),(-.030,0,.327),(-.040,0,.345)]
    tube('Continuous bent alloy shaft',interpolate(points,2),.024,titanium,40)
    lathe('Shaft lower bearing race',[(.083,.0245),(.086,.0265),(.092,.0265),(.095,.0245)],titanium,sides=48)


def make_uv(obj):
    # Physical .25m repeat. Dominant-axis projection per triangle keeps a nonzero
    # UV Jacobian even on tiny curved mechanical features and every cap.
    for layer in list(obj.data.uv_layers):
        obj.data.uv_layers.remove(layer)
    uv=obj.data.uv_layers.new(name='UVMap')
    uv.active_render=True
    for p in obj.data.polygons:
        for li in p.loop_indices:
            co=obj.data.vertices[obj.data.loops[li].vertex_index].co
            dominant=max(range(3),key=lambda k:abs(p.normal[k]))
            if dominant==2:
                value=(co.x/.25,co.y/.25)
            elif dominant==0:
                value=(co.y/.25,co.z/.25)
            else:
                value=(co.x/.25,co.z/.25)
            uv.data[li].uv=value


def camera(name,loc,target,ortho):
    bpy.ops.object.camera_add(location=loc)
    obj=bpy.context.object; obj.name=name
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    obj.data.type='ORTHO';obj.data.ortho_scale=ortho;obj.data.lens=58
    return obj


def render_compare(old,new):
    scene=bpy.context.scene; scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=32
    scene.cycles.use_denoising=True;scene.render.threads_mode='FIXED';scene.render.threads=4
    scene.render.resolution_x=1200;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
    scene.world.color=(.16,.16,.16)
    scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
    for name,loc,energy,size in [('Key',(.45,-.6,1.1),55,.55),('Softbox',(-.6,.3,.8),38,.6),('Rim',(.25,.6,.4),25,.35)]:
        bpy.ops.object.light_add(type='AREA',location=loc)
        o=bpy.context.object;o.name=name;o.data.energy=energy;o.data.shape='DISK';o.data.size=size
        o.rotation_euler=(Vector((-.03,0,.3))-o.location).to_track_quat('-Z','Y').to_euler()
    views={'close':{'location':(.54,-.73,.68),'target':(-.047,0,.435),'ortho':.29},
           'side':{'location':(.02,-1.0,.56),'target':(-.02,0,.245),'ortho':.72}}
    for name,v in views.items():
        scene.camera=camera('Review_'+name,v['location'],v['target'],v['ortho'])
        for label,ob,hidden in [('old',old,new),('new',new,old)]:
            ob.hide_render=False;hidden.hide_render=True
            scene.render.filepath=str(OUT/'Review'/(label+'_'+name+'.png'))
            bpy.ops.render.render(write_still=True)
            record('progress',{'stage':'rendered','image':label+'_'+name})
    old.hide_render=True;new.hide_render=False
    return views


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--source-root',required=True);parser.add_argument('--no-render',action='store_true')
    opts=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    source=Path(opts.source_root)
    OUT.mkdir(parents=True,exist_ok=True);WORK.mkdir(parents=True,exist_ok=True);(OUT/'Review').mkdir(exist_ok=True)
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    scene.render.threads_mode='FIXED';scene.render.threads=4
    original=source/'Content/Star/Art/ExplorerV2/Parts/SM_ControlStick.fbx'
    bpy.ops.import_scene.fbx(filepath=str(original),use_custom_normals=True)
    old=next(o for o in bpy.context.selected_objects if o.type=='MESH')
    old.name='CONTROL_BASELINE_V2'
    old_info=inspect(old)
    record('baseline',old_info)
    recipe={
        'M_CockpitSoftTouch':{'base_color_linear':[.025,.031,.035],'metallic':0.0,'roughness':.67},
        'M_GraphiteStructure':{'base_color_linear':[.012,.017,.020],'metallic':0.0,'roughness':.49},
        'M_Titanium':{'base_color_linear':[.22,.26,.29],'metallic':.93,'roughness':.30},
        'M_MachinedAlloy':{'base_color_linear':[.49,.54,.57],'metallic':.97,'roughness':.23},
        'M_MutedAmber':{'base_color_linear':[.55,.21,.032],'metallic':.45,'roughness':.33}}
    mats={name:material(name,value) for name,value in recipe.items()}
    polymer=mats['M_CockpitSoftTouch'];dark=mats['M_GraphiteStructure'];titanium=mats['M_Titanium'];alloy=mats['M_MachinedAlloy'];amber=mats['M_MutedAmber']
    build_base(polymer,dark,titanium,alloy);build_grip(polymer,dark,titanium,alloy,amber)
    # Apply all authoring transforms and join at inherited pivot, never at world ship origin.
    bpy.ops.object.select_all(action='DESELECT')
    for obj in PARTS:obj.select_set(True)
    bpy.context.view_layer.objects.active=PARTS[0]
    bpy.ops.object.convert(target='MESH');bpy.ops.object.join()
    new=bpy.context.object;new.name='SM_ControlStick'
    scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    # Export true triangulated faces, retaining authored split/corner normals.
    tri=new.modifiers.new('Explicit export triangles','TRIANGULATE');tri.keep_custom_normals=True;apply(new,tri)
    bm=bmesh.new();bm.from_mesh(new.data)
    bmesh.ops.dissolve_degenerate(bm,dist=1e-7,edges=list(bm.edges))
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(new.data);bm.free();new.data.update()
    make_uv(new)
    new.data.calc_tangents(uvmap='UVMap')
    info=inspect(new)
    lo,hi=old_info['bounds_m']['min'],old_info['bounds_m']['max']
    outside=[list(v.co) for v in new.data.vertices if any(v.co[i]<lo[i]-1e-5 or v.co[i]>hi[i]+1e-5 for i in range(3))]
    checks={'triangle_budget':info['triangles']<=30000,'finite_vertices':info['nonfinite_vertices']==0,
            'closed_components':info['boundary_edges']==info['nonmanifold_edges']==info['wire_edges']==0,
            'nondegenerate_faces':info['zero_area_faces']==0,'unit_normals':info['invalid_corner_normals']==0,
            'inherited_aabb_contained':not outside,
            'identity_export_transform':all(abs(x)<1e-7 for x in list(new.location)+list(new.rotation_euler)) and all(abs(x-1)<1e-7 for x in new.scale),
            'finite_tangents':all(all(math.isfinite(x) for x in l.tangent) and l.tangent.length>.99 for l in new.data.loops),
            'single_authored_uv0':len(new.data.uv_layers)==1 and new.data.uv_layers[0].name=='UVMap',
            'shaft_frame_continuity':bool(TUBE_FRAME_CHECKS) and all(c['adjacent_frame_min_dot']>.98 for c in TUBE_FRAME_CHECKS)}
    validation={'checks':checks,'new':info,'old':old_info,'out_of_bounds_examples':outside[:10],
                'evidence_gate':'LOCAL_ASSET_VALIDATION_ONLY','not_verified':['UE imported shading','packaged game appearance','physical joystick'],
                'intersection_policy':'multiple closed manufactured subcomponents overlap at concealed assembly joints; no CSG union required',
                'tube_frame_checks':TUBE_FRAME_CHECKS}
    (OUT/'validation.json').write_text(json.dumps(validation,indent=2),encoding='utf-8')
    if not all(checks.values()):raise RuntimeError('Mesh validation failed: '+str(checks))
    bpy.ops.object.select_all(action='DESELECT');new.select_set(True);bpy.context.view_layer.objects.active=new
    bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_ControlStick.fbx'),use_selection=True,
        axis_forward='X',axis_up='Z',global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',
        object_types={'MESH'},bake_anim=False,use_mesh_modifiers=True,use_tspace=True,mesh_smooth_type='FACE',path_mode='RELATIVE')
    bpy.ops.export_scene.gltf(filepath=str(OUT/'SM_ControlStick.glb'),export_format='GLB',use_selection=True,
        export_yup=True,export_apply=True,export_texcoords=True,export_normals=True,export_tangents=True,export_materials='EXPORT')
    manifest={'asset':'Original STAR ergonomic flight control V4','source_date':'2026-09-07','source_units':'meters',
        'source_frame':{'forward':'+X','right':'+Y','up':'+Z'},'fbx_axes':{'forward':'X','up':'Z'},
        'glb_axes':'standard glTF +Y up, exporter transforms Blender +Z up; import converts back explicitly',
        'part_name':'SM_ControlStick','local_export_origin_m':[0,0,0],'assembly_pivot_m':PIVOT,
        'assembly_rotation_degrees':[0,0,0],'assembly_scale':[1,1,1],
        'runtime_location_cm':[p*100 for p in PIVOT],'source_baseline':str(original.relative_to(source)),
        'baseline_sha256':digest(original),'build_script_sha256':digest(Path(__file__)),
        'materials_existing_slots':recipe,'new_materials_required':False,
        'surface_detail':'Original submillimeter mould seam and finger valleys are modeled geometry. No observational detail is claimed.',
        'control_details_mm':{'shell_seam_depth':.5,'palm_inlay_height':.8,'typical_edge_radius':1,'trigger_edge_radius':5,
                              'shaft_diameter':48,'trigger_cap_width':40,'thumb_hat_width':28},
        'rights':{'geometry':'Original fictional STAR design, authored procedurally; no brand or NASA logo.',
                 'reference':'Inspected existing NASA Orion cockpit mockup image jsc2022e044163 (2014-10-24, Robert Markowitz/NASA); contextual arrangement only, not dimensional reconstruction.',
                 'reference_source_url':'https://images.nasa.gov/details/jsc2022e044163','reference_bytes_distributed':False},
        'provenance':'Existing V2 part establishes pivot, frame and occupied AABB. Original design is not measured flight hardware.',
        'envelope_policy':'New AABB is contained by inherited original AABB; local grip/shaft respect conservative original envelope. Root verifies moving-control clearances in engine.',
        'validation':'validation.json','import':'Root only: UE Import Normals and Tangents, MikkTSpace, remove degenerates, scale1; retain existing placement. No automatic collision or material replacement.',
        'evidence_gate':'LOCAL_ASSET_VALIDATION_ONLY'}
    record('progress',{'stage':'exports_ready','triangles':info['triangles']})
    if not opts.no_render:manifest['review_cameras']=render_compare(old,new)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'STAR_ControlStickV4.blend'))
    manifest['artifact_sha256']={str(p.relative_to(OUT)):digest(p) for p in OUT.rglob('*') if p.is_file() and p.name not in ('manifest.json','export_validation.json') and not p.name.endswith('.blend1')}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    record('progress',{'stage':'complete','triangles':info['triangles']})


if __name__=='__main__':
    try:main()
    except Exception:
        WORK.mkdir(parents=True,exist_ok=True)
        (WORK/'error.txt').write_text(traceback.format_exc(),encoding='utf-8')
        raise
