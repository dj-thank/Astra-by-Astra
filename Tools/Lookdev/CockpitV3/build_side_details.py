"""Independent cockpit cheek lining. Existing V3 FBX files are read-only.
Geometry registers to the actual inner shell triangles, not screenshot billboards.
"""
from pathlib import Path
import bpy,math,json,hashlib,bmesh
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[3];ART=ROOT/'Art/Explorer/CockpitV3';OUT=ROOT/'Content/Star/Art/CockpitV3'
bpy.ops.wm.open_mainfile(filepath=str(ART/'STAR_CockpitV3.blend'))
scene=bpy.context.scene;source=bpy.data.objects['SM_CockpitShell'];camera=Vector((6,0,1.3));objects=[]
materials={k:bpy.data.materials[k] for k in ('M_CV3_AnodizedGraphite','M_CV3_Elastomer','M_CV3_WovenSeat','M_CV3_BeadBlastedAlloy','M_CV3_EtchedLegends','M_CV3_InsulationPhoto')}
dark=materials['M_CV3_AnodizedGraphite'];rubber=materials['M_CV3_Elastomer'];cloth=materials['M_CV3_WovenSeat'];metal=materials['M_CV3_BeadBlastedAlloy'];ink=materials['M_CV3_EtchedLegends'];foil=materials['M_CV3_InsulationPhoto']
def register(o,name,mat):
 o.name=name;o['recon_part']='side-details';o.data.materials.append(mat);objects.append(o);return o
def mesh(name,verts,faces,mat,bevel=0,smooth=False):
 me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update()
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free()
 o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);register(o,name,mat)
 uv=me.uv_layers.new(name='UVMap')
 for p in me.polygons:
  p.use_smooth=smooth
  for li in p.loop_indices:
   co=me.vertices[me.loops[li].vertex_index].co
   uv.data[li].uv=(co.dot(U),co.dot(V))
 if bevel:
  b=o.modifiers.new('Edge return radius','BEVEL');b.width=bevel;b.segments=3
  w=o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');w.keep_sharp=True
 return o
def plate(name,points,mat,thick=.012,bevel=.002):
 pts=[Vector(p) for p in points]
 if (pts[1]-pts[0]).cross(pts[2]-pts[0]).dot(N)<0:pts.reverse()
 k=len(pts);vv=pts+[p-N*thick for p in pts]
 ff=[tuple(range(k)),tuple(range(2*k-1,k-1,-1))]+[(i,(i+1)%k,(i+1)%k+k,i+k) for i in range(k)]
 return mesh(name,vv,ff,mat,bevel)
def box(name,p,size,mat=dark):
 bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.scale=size
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 o.rotation_euler=Matrix((U,V,N)).transposed().to_euler();register(o,name,mat)
 for f in o.data.polygons:f.use_smooth=True
 b=o.modifiers.new('Rounded manufacture','BEVEL');b.width=min(size)*.12;b.segments=3
 w=o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');w.keep_sharp=True
 return o
def rod(name,a,b,r=.008,mat=metal):
 a,b=Vector(a),Vector(b);delta=b-a
 bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=r,depth=delta.length,location=(a+b)/2)
 o=bpy.context.object;o.rotation_euler=delta.to_track_quat('Z','Y').to_euler()
 for p in o.data.polygons:p.use_smooth=True
 return register(o,name,mat)
def fastener(p):
 rod('Captive liner fastener seat',p-N*.001,p+N*.0005,.006,rubber)
 rod('Captive liner fastener head',p,p+N*.002,.0045,metal)
 box('Fastener drive recess',p+N*.0023,(.005,.001,.0005),rubber)
def pad(name,t0,t1,f0,f1,mat):
 nx,ny=10,4;verts=[]
 for j in range(ny+1):
  for i in range(nx+1):
   u=i/nx;v=j/ny
   # Slight irregular soft backing supported by the shell, no invented damage.
   bulge=.012*math.sin(math.pi*u)*math.sin(math.pi*v)*(1+.14*math.sin(u*9))
   verts.append(at(t0+(t1-t0)*u,f0+(f1-f0)*v)+N*(.023+bulge))
 faces=[]
 for j in range(ny):
  for i in range(nx):
   a=j*(nx+1)+i;faces.append((a,a+1,a+nx+2,a+nx+1))
 if (verts[1]-verts[0]).cross(verts[nx+2]-verts[0]).dot(N)<0:faces=[tuple(reversed(f)) for f in faces]
 o=mesh(name,verts,faces,mat,smooth=True)
 mod=o.modifiers.new('Stitched cushion thickness','SOLIDIFY');mod.thickness=.016
 # Photo patch remains a small material region; inferred blanket geometry.
 if mat==foil:
  uv=o.data.uv_layers.active
  for face in o.data.polygons:
   for li in face.loop_indices:
    idx=o.data.loops[li].vertex_index;uv.data[li].uv=((idx%(nx+1))/nx,(idx//(nx+1))/ny)
 return o
source_faces=[]
for face in source.data.polygons:
 if face.area>3.0 and face.area<3.5 and face.normal.z<-.7 and abs(face.normal.y)>.5:
  pts=[source.matrix_world@source.data.vertices[i].co for i in face.vertices]
  if len(pts)!=3:continue
  B=min(pts,key=lambda p:p.x);C=max(pts,key=lambda p:p.x);D=next(p for p in pts if p!=B and p!=C)
  N=face.normal.copy().normalized();U=(C-B).normalized();V=N.cross(U).normalized()
  def at(t,f):return B*(1-t)+(C*(1-f)+D*f)*t
  source_faces.append({'vertices_m':[list(B),list(C),list(D)],'normal':list(N),'construction':'Inner-facing existing cheek triangle with positive normal offset into cabin'})
  # A shaped support pan follows the actual triangle, inset from optical boundary.
  skin=[at(.05,.06),at(.93,.06),at(.93,.91),at(.05,.91)]
  plate('Shaped cheek support pan',[p+N*.012 for p in skin],dark,.012,.003)
  # Bound edges and internal ties look manufactured at grazing sun angles.
  for f in (.085,.88):
   rod('Cheek liner rolled perimeter',at(.10,f)+N*.023,at(.91,f)+N*.023,.009,dark)
  for t in (.13,.30,.47,.64,.81,.91):
   rod('Liner transverse tie',at(t,.09)+N*.022,at(t,.88)+N*.022,.006,metal)
   for f in (.105,.865):fastener(at(t,f)+N*.03)
  # Main flexible acoustic/thermal lining, interrupted at real tie-down seams.
  for t0,t1 in ((.135,.292),(.307,.462),(.477,.632),(.647,.802),(.817,.904)):
   pad('Sewn cheek acoustic liner',t0,t1,.115,.60,cloth)
   pad('Lower restrained thermal blanket',t0,t1,.65,.85,foil if t0>.47 else cloth)
  # Small service modules sit wholly inside the old opaque cheek projection.
  for t,body in ((.31,'ECLSS'),(.49,'SERVICE')):
   p=at(t,.39)+N*.047
   box('Bonded service cassette backing',p,(.27,.15,.018),rubber)
   box('Folded service cassette housing',p+N*.016,(.248,.131,.03),dark)
   box('Inset equipment ID plate',p+N*.033,(.155,.037,.0016),ink)
   font=bpy.data.curves.new('Equipment legend','FONT');font.body=body;font.size=.014;font.align_x='CENTER';font.align_y='CENTER'
   o=bpy.data.objects.new('Equipment fixed legend',font);bpy.context.collection.objects.link(o);o.location=p+N*.0345;o.rotation_euler=Matrix((U,V,N)).transposed().to_euler();register(o,'Equipment fixed legend',dark)
   for du in (-.109,.109):
    for dv in (-.047,.047):fastener(p+U*du+V*dv+N*.034)
  # Cabled edge run is anchored at tie points, kept below canopy rim.
  for offset in (0,.018):
   p0=None
   for i in range(15):
    t=.16+i*.044;p=at(t,.62)+N*(.045+offset)
    if p0 is not None:rod('Clamped sleeve run',p0,p,.006,rubber)
    p0=p
  for t in (.2,.37,.54,.71):
   p=at(t,.62)+N*.04;box('Sleeve retaining clamp',p,(.027,.032,.012),metal);fastener(p+N*.009)

# Only the new independent mesh is exported, at the unchanged ship origin.
bpy.ops.object.select_all(action='DESELECT')
for o in objects:o.select_set(True)
bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.convert(target='MESH');bpy.ops.object.join();o=bpy.context.object;o.name='SM_CockpitV3SideDetails';o['recon_part']='side-details'
scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
o.data.calc_loop_triangles();vertices=[o.matrix_world@v.co for v in o.data.vertices]
bounds={'min':[min(v[i] for v in vertices) for i in range(3)],'max':[max(v[i] for v in vertices) for i in range(3)]}
fbx=OUT/'Parts/SM_CockpitV3SideDetails.fbx'
bpy.ops.export_scene.fbx(filepath=str(fbx),use_selection=True,axis_forward='X',axis_up='Z',global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',object_types={'MESH'},bake_anim=False,path_mode='RELATIVE',use_mesh_modifiers=True)
# Sample actual rays against the new geometry only; original open sky must stay open.
blocked=[]
for cy in (-.88,0,.88):
 for u in (-.95,-.5,0,.5,.95):
  for v in (-.95,-.5,0,.5,.95):
   dest=Vector((6.838,cy+u*.619/2,.747+v*.312/2));direction=dest-camera
   hit,loc,n,idx=o.ray_cast(camera,direction.normalized(),distance=direction.length-.0005)
   if hit:blocked.append(list(dest))
open_sky_occluded=[];checked=0
rot=Matrix.Rotation(math.radians(12),4,'Y')
for ix in range(61):
 for iy in range(31):
  nx=-.98+ix*1.96/60;ny=-.98+iy*1.45/30
  direction=(rot@Vector((1,-nx*math.tan(math.radians(50)),-ny*math.tan(math.radians(50))/(16/9)))).normalized()
  hit,*_=o.ray_cast(camera,direction)
  if hit:
   checked+=1;old,*_=source.ray_cast(camera,direction)
   if not old:open_sky_occluded.append([ix,iy])
report={'name':o.name,'fbx':'Parts/'+fbx.name,'source_units':'meters','source_frame':{'forward':'+X','right':'+Y','up':'+Z'},'location_ue_cm':[0,0,0],'rotation_ue_deg':[0,0,0],'scale':[1,1,1],'bounds_m':bounds,'bounds_ue_cm':{k:[v*100 for v in values] for k,values in bounds.items()},'triangles':len(o.data.loop_triangles),'materials':[m.name for m in o.data.materials],'sha256':hashlib.sha256(fbx.read_bytes()).hexdigest(),'registered_shell_triangles':source_faces,'display_rays':{'sample_count':75,'blocked':blocked},'sky_rays':{'grid_count':1891,'new_mesh_hits':checked,'previously_open_sky_occluded':open_sky_occluded},'status':'LOCAL_GEOMETRY_PASS' if not blocked and not open_sky_occluded else 'FAIL','reference':'NASA jsc2022e044970 construction: dark window surrounds, restrained insulation, removable equipment cassettes. Original ASTER 24 geometry inference.','existing_two_fbx':'not rewritten','remaining':'Actual UE lighting/material application and independent visual acceptance are root work.'}
(ROOT/'Data/cockpit_v3_side_details.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(ART/'STAR_CockpitV3SideDetails.blend'))
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24;scene.render.threads_mode='FIXED';scene.render.threads=4
scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100
cam=scene.camera;cam.location=camera;cam.rotation_euler=(Vector((math.cos(math.radians(12)),0,-math.sin(math.radians(12))))).to_track_quat('-Z','Y').to_euler();cam.data.type='PERSP';cam.data.sensor_fit='HORIZONTAL';cam.data.lens=cam.data.sensor_width/(2*math.tan(math.radians(50)))
scene.render.filepath=str(ART/'Renders/side_details_front.png');bpy.ops.render.render(write_still=True)
report['render']={'file':'Renders/side_details_front.png','camera_m':[6,0,1.3],'pitch_deg':-12,'hfov_deg':100,'sha256':hashlib.sha256(Path(scene.render.filepath).read_bytes()).hexdigest()}
(ROOT/'Data/cockpit_v3_side_details.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2),flush=True)
