"""ASTER 24 manufactured cockpit candidate. CPU-only, independent Blender process.
New assets only. Coordinates in m, +X forward +Y right +Z up. Original design.
NASA mockup photos inform assembly; only explicitly mapped insulation uses pixels.
"""
from pathlib import Path
import bpy, math, json, sys, hashlib
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[3]
ART=ROOT/'Art/Explorer/CockpitV3'
OUT=ROOT/'Content/Star/Art/CockpitV3'
for p in (ART/'Renders',OUT/'Parts',OUT/'Textures'): p.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24
scene.cycles.use_denoising=True;scene.render.threads_mode='FIXED';scene.render.threads=4
scene.view_settings.view_transform='AgX'
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.18,.23,.3,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.22
parts={}; specs={}; group='SM_CockpitV3Interior'
def mat(name,c,metal=0,rough=.5):
 m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*c,1)
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
 specs[name]={'base_color_linear':list(c),'metallic':metal,'roughness':rough}
 return m
ivory=mat('M_CV3_PowderCoat',(.38,.40,.37),0,.58)
dark=mat('M_CV3_AnodizedGraphite',(.052,.064,.071),.72,.36)
alloy=mat('M_CV3_BeadBlastedAlloy',(.38,.42,.44),.94,.29)
rubber=mat('M_CV3_Elastomer',(.009,.013,.016),0,.78)
cloth=mat('M_CV3_WovenSeat',(.105,.123,.121),0,.9)
labelmat=mat('M_CV3_EtchedLegends',(.69,.73,.7),0,.57)
amber=mat('M_CV3_SafetyOchre',(.56,.28,.038),0,.49)
blue=mat('M_CV3_ServiceBlue',(.014,.07,.16),0,.48)
screen=mat('M_CV3_DisplayOff',(.001,.003,.004),0,.28)
foil=mat('M_CV3_InsulationPhoto',(.32,.24,.10),0,.84)
for m,key in ((ivory,'Coat'),(alloy,'Alloy'),(dark,'Graphite'),(cloth,'Cloth')):
 nt=m.node_tree;p=nt.nodes.get('Principled BSDF')
 im=bpy.data.images.load(str(OUT/'Textures'/f'{key}_Roughness.png'));im.colorspace_settings.name='Non-Color'
 tex=nt.nodes.new('ShaderNodeTexImage');tex.image=im;nt.links.new(tex.outputs['Color'],p.inputs['Roughness'])
 specs[m.name]['roughness_texture']=f'Textures/{key}_Roughness.png'
 if m==cloth:
  tex=nt.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(OUT/'Textures/Cloth_Normal.png'));tex.image.colorspace_settings.name='Non-Color'
  uvnode=nt.nodes.new('ShaderNodeTexCoord');scale=nt.nodes.new('ShaderNodeVectorMath');scale.operation='SCALE';scale.inputs[3].default_value=10
  nt.links.new(uvnode.outputs['UV'],scale.inputs[0]);nt.links.new(scale.outputs[0],tex.inputs['Vector'])
  normal=nt.nodes.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value=.22;nt.links.new(tex.outputs['Color'],normal.inputs['Color']);nt.links.new(normal.outputs['Normal'],p.inputs['Normal'])
  specs[m.name].update({'normal_texture':'Textures/Cloth_Normal.png','normal_strength':.22,'normal_uv_repeat':10})
def reg(o,name,m,bevel=0):
 o.name=name;o['recon_part']=group.lower().replace('_','-')
 if m:o.data.materials.append(m)
 if bevel:
  if o.type=='MESH':
   for face in o.data.polygons:face.use_smooth=True
  mod=o.modifiers.new('Manufactured edge radius','BEVEL');mod.width=bevel;mod.segments=3
  mod=o.modifiers.new('Weighted face normals','WEIGHTED_NORMAL');mod.keep_sharp=True
 parts.setdefault(group,[]).append(o)
 return o
def box(name,loc,size,m=dark,b=.002):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.scale=size
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 return reg(o,name,m,b)
def cyl(name,a,b,r,m=alloy,n=24):
 a,b=Vector(a),Vector(b);d=b-a
 bpy.ops.mesh.primitive_cylinder_add(vertices=n,radius=r,depth=d.length,location=(a+b)/2)
 o=bpy.context.object;o.rotation_euler=d.to_track_quat('Z','Y').to_euler()
 for p in o.data.polygons:p.use_smooth=True
 return reg(o,name,m,.0005)
def tube(name,pts,r,m=alloy):
 cv=bpy.data.curves.new(name,'CURVE');cv.dimensions='3D';cv.resolution_u=12;cv.bevel_depth=r;cv.bevel_resolution=3
 sp=cv.splines.new('BEZIER');sp.bezier_points.add(len(pts)-1)
 for p,co in zip(sp.bezier_points,pts):p.co=co;p.handle_left_type='AUTO';p.handle_right_type='AUTO'
 o=bpy.data.objects.new(name,cv);bpy.context.collection.objects.link(o);return reg(o,name,m)
def text(name,body,pos,size=.013,rot=(0,0,0),m=labelmat):
 cv=bpy.data.curves.new(name,'FONT');cv.body=body;cv.size=size;cv.extrude=.00012;cv.align_x='CENTER'
 o=bpy.data.objects.new(name,cv);bpy.context.collection.objects.link(o);o.location=pos;o.rotation_euler=rot
 return reg(o,name,m)
rear=(math.pi/2,0,-math.pi/2)
def fastener(p,axis='Z'):
 p=Vector(p);n=Vector((0,0,1) if axis=='Z' else (-1,0,0) if axis=='X' else (0,1,0))
 cyl('Recessed captive screw washer',p-n*.001,p+n*.0005,.0065,dark)
 cyl('Captive screw head',p,p+n*.0014,.0045,alloy)
 o=box('Screw drive slot',p+n*.0017,(.006,.0011,.0004),rubber,.0001)
 if axis=='X':o.rotation_euler.y=math.pi/2
 if axis=='Y':o.rotation_euler.x=math.pi/2
def panel_top(name,x,y,sx,sy,z=1.002):
 box(name+' gasket',(x,y,z-.005),(sx+.01,sy+.01,.005),rubber)
 box(name+' folded face',(x,y,z),(sx,sy,.006),dark)
 for dx in (-sx/2+.018,sx/2-.018):
  for dy in (-sy/2+.018,sy/2-.018):fastener((x+dx,y+dy,z+.004))
def toggle(x,y,z,guard=True):
 cyl('Switch threaded bushing',(x,y,z),(x,y,z+.009),.009,alloy,20)
 cyl('Switch stem',(x,y,z+.008),(x+.009,y,z+.035),.0036,alloy,16)
 cyl('Toggle polymer tip',(x+.007,y,z+.027),(x+.011,y,z+.039),.0055,labelmat,16)
 if guard:
  for dy in (-.018,.018):tube('U switch finger guard',[(x-.02,y+dy,z+.002),(x-.02,y+dy,z+.043),(x+.023,y+dy,z+.043),(x+.023,y+dy,z+.002)],.003,alloy)
def knob(x,y,z,r=.019):
 cyl('Encoder threaded base',(x,y,z),(x,y,z+.006),r*1.2,alloy)
 cyl('Fluted encoder',(x,y,z+.006),(x,y,z+.03),r,dark,32)
 for i in range(16):
  a=i*math.tau/16;cyl('Encoder grip fluting',(x+math.cos(a)*r,y+math.sin(a)*r,z+.009),(x+math.cos(a)*r,y+math.sin(a)*r,z+.026),.0017,rubber,8)
 box('Encoder index',(x+r*.4,y,z+.031),(r*.8,.002,.001),labelmat,.0002)
def webbing(name,a,b,width=.046):
 a,b=Vector(a),Vector(b);d=b-a
 o=box(name,(a+b)/2,(.005,width,d.length),rubber,.001)
 o.rotation_euler=d.to_track_quat('Z','Y').to_euler()
 return o

# Three display openings retain the old exact writable surfaces and widget normals.
group='SM_CockpitV3InstrumentPanel'
box('Console structural tub',(7.24,0,.52),(.58,2.65,.59),ivory,.018)
box('Console service split',(6.941,0,.36),(.012,2.62,.011),rubber)
box('Glare shield aluminum core',(7.15,0,.985),(.61,2.68,.034),dark,.009)
box('Glare shield soft front lip',(6.842,0,1.006),(.055,2.68,.042),rubber,.014)
for y in (-.88,0,.88):
 # Four bars make a real opening; never put a decorative plane over live widgets.
 for z in (.54,.943):box('Display bezel horizontal',(6.864,y,z),(.068,.78,.041),dark,.008)
 for sy in (-1,1):box('Display bezel vertical',(6.864,y+sy*.369,.743),(.068,.042,.365),dark,.007)
 for z in (.581,.912):box('Elastomer screen reveal',(6.842,y,z),(.012,.658,.012),rubber,.002)
 for sy in (-1,1):box('Elastomer screen reveal',(6.842,y+sy*.324,.747),(.012,.012,.322),rubber,.002)
 box('Unpopulated live display surface',(6.843,y,.747),(.006,.619,.312),screen,.003)
 for sy in (-.349,.349):
  for z in (.564,.923):fastener((6.826,y+sy,z),'X')
 for i in range(9):
  yy=y-.264+i*.066
  box('Soft key socket',(6.824,yy,.548),(.013,.044,.022),rubber,.002)
  box('Tactile soft key',(6.814,yy,.549),(.012,.036,.014),dark,.002)
  box('Soft key index',(6.807,yy,.55),(.0008,.016,.001),labelmat,.0001)
 for sy in (-1,1):
  yy=y+sy*.351
  cyl('MFD encoder collar',(6.834,yy,.619),(6.815,yy,.619),.022,alloy)
  cyl('MFD encoder knob',(6.815,yy,.619),(6.789,yy,.619),.018,dark,32)
  for i in range(16):
   a=i*math.tau/16
   cyl('MFD encoder knurl',(6.812,yy+.018*math.cos(a),.619+.018*math.sin(a)),(6.793,yy+.018*math.cos(a),.619+.018*math.sin(a)),.0015,rubber,8)
 text('MFD fixed identifier','DU '+str(round((y+.88)/.88)+1),(6.826,y,.938),.015,rear)
 # Physical vent slots in shroud, created as spaced ribs over dark recess.
 box('Display cooling recess',(7.106,y,1.005),(.25,.46,.015),rubber,.006)
 for i in range(14):box('Cooling grille rib',(7.106,y-.214+i*.033,1.015),(.24,.012,.008),dark,.002)
 for sy in (-.32,.32):fastener((7.185,y+sy,1.005))
 for sy in (-.29,.29):fastener((6.941,y+sy,.42),'X')
 # Separate wired control strip with mechanical pushbuttons, no invented telemetry.
 box('Control strip folded housing',(7.34,y,1.055),(.12,.49,.10),dark,.008)
 box('Control strip rear gasket',(7.274,y,1.055),(.007,.464,.079),rubber,.002)
 for idx in range(5):
  yy=y-.176+idx*.088
  box('Recessed control switch body',(7.268,yy,1.063),(.014,.068,.054),alloy,.003)
  box('Recessed control cap',(7.258,yy,1.063),(.012,.057,.043),rubber,.004)
  legends=('COM','NAV','RANGE','TRACK','AUX') if y<-.1 else ('BUS A','BUS B','ECLSS','RCS','SAFE') if y<.1 else ('CAM','SPEC','SCAN','DATA','AUX')
  text('Fixed control label',legends[idx],(7.25,yy,1.066),.009,rear)
 for yy in (y-.228,y+.228):fastener((7.272,yy,1.054),'X')
text('Console identification','ASTER 24  /  DISPLAY CONTROL ASSEMBLY',(6.934,0,.409),.017,rear)

# Side consoles: removable trays, guard rails, serviceable wiring, measured switches.
group='SM_CockpitV3Interior'
for side in (-1,1):
 y=side*1.245
 # Forward tray narrows outboard before the outer MFD sight lines.
 box('Console structural casing aft',(5.82,y,.566),(1.48,.36,.68),ivory,.018)
 box('Console structural casing forward',(7.07,side*1.335,.566),(1.02,.14,.68),ivory,.012)
 box('Console top elastomer seam aft',(5.82,y,.914),(1.47,.369,.01),rubber,.003)
 box('Folded console upper rim aft',(5.82,y,.925),(1.48,.38,.012),alloy,.003)
 box('Console top elastomer seam forward',(7.07,side*1.32,.914),(1.02,.18,.01),rubber,.003)
 box('Folded console upper rim forward',(7.07,side*1.32,.925),(1.02,.19,.012),alloy,.003)
 for i,x in enumerate((5.37,5.89,6.43,6.97,7.43)):
  sx=.43 if i<4 else .31
  panel_y=y if i<3 else side*1.32
  panel_top('Side LRU '+str(i),x,panel_y,sx,.31 if i<3 else .17,.95)
  for j,yy in enumerate((panel_y-(.075 if i<3 else .035),panel_y+(.075 if i<3 else .035))):
   if i in (0,3):
    knob(x,yy,.954)
    text('Rotary legend',('LIGHT','DIM') [j],(x,yy-.041,.955),.011)
   else:
    for xx in (x-.073,x+.073):toggle(xx,yy,.954)
  text('Panel system legend',('ECLSS','POWER A / B','RCS / ATT','COMMS','AUX')[i],(x,panel_y+(.122 if i<3 else .064),.955),.013)
 # Inner wall removable covers above console retain clear sight line through canopy.
 for i,x in enumerate((5.58,6.17,6.76,7.35)):
  wall_y=side*(1.40-(x-5.58)*.08)
  height=.26 if x<7 else .20
  o=box('Inner pressure liner',(x,wall_y,.97+height/2),(.56,.022,height),ivory,.007)
  for xx in (x-.23,x+.23):
   box('Liner support bracket',(xx,wall_y+side*.021,.99+height/2),(.024,.042,height-.025),dark,.003)
  for xx in (x-.248,x+.248):
   for zz in (1.002,.952+height):
    fastener((xx,wall_y-side*.014,zz),'Y')
  box('Liner recessed service split',(x+.289,wall_y,1.04),(.006,.026,.16),rubber,.001)
 # Cable bundle follows rail; every run terminates at a keyed connector.
 for j in range(4):
  yy=y-side*(.212+j*.008)
  tube('Avionics routed cable',[(5.18,yy,.72),(5.35,yy,.78),(6.05,yy,.785),(6.48,yy,.76),(6.60,side*(1.28-j*.008),.75),(7.42,side*(1.28-j*.008),.69)],.0035,blue if j==0 else rubber)
 for x in (5.42,5.95,6.48,7.08):
  clamp_y=y-side*.227 if x<6.6 else side*1.268
  box('Cable P clamp foot',(x,clamp_y,.77),(.028,.053,.016),alloy,.003)
  fastener((x,clamp_y,.78))
 for x,z in ((5.18,.72),(7.42,.69)):
  connector_y=y-side*.22 if x<6.6 else side*1.268
  cyl('Cable bulkhead connector',(x-.018,connector_y,z),(x+.018,connector_y,z),.024,alloy)
  cyl('Connector backshell boot',(x+.02,connector_y,z),(x+.047,connector_y,z),.018,rubber)
 tube('Ergonomic grab rail',[(5.16,y-side*.04,1.02),(5.22,y-side*.04,1.1),(5.65,y-side*.04,1.1),(5.71,y-side*.04,1.02)],.013,alloy)
 for x in (5.16,5.71):box('Grab rail bolted foot',(x,y-side*.04,1.017),(.066,.056,.017),dark,.005)
 # Insulation access recess below side equipment; a genuine photo region, not noise.
 box('Thermal access frame',(5.98,y-side*.19,.485),(.74,.024,.26),dark,.006)
 for xx in (5.74,5.98,6.22):
  box('Photo backed quilted insulation cushion',(xx,y-side*.213,.485),(.235,.030,.218),foil,.027)
 for xx in (5.86,6.10):
  box('Insulation quilt seam',(xx,y-side*.233,.485),(.005,.004,.203),rubber,.001)
 for x in (5.65,6.31):box('Insulation retaining strap',(x,y-side*.215,.485),(.016,.01,.218),ivory,.002)

# Preserved seating envelope with shaped bolsters, harness and manufacturing seams.
for y in (-.62,.79):
 box('Seat pedestal',(5.06,y,.37),(.63,.72,.44),dark,.016)
 box('Seat pan cushion',(5.28,y,.67),(.94,.61,.16),cloth,.07)
 o=box('Seat back structural shell',(4.685,y,1.12),(.08,.62,.91),dark,.023);o.rotation_euler.y=-.14
 for z in (.79,1.015,1.24,1.45):
  o=box('Individually sewn back cushion',(4.86-(z-.79)*.14,y,z),(.17,.55,.205 if z<1.4 else .17),cloth,.035);o.rotation_euler.y=-.14
 box('Head restraint',(4.70,y,1.65),(.21,.48,.29),cloth,.055)
 for s in (-1,1):
  box('Seat lateral bolster',(5.25,y+s*.30,.72),(.81,.095,.19),cloth,.038)
  o=box('Shoulder bolster',(4.80,y+s*.30,1.2),(.24,.12,.55),cloth,.045);o.rotation_euler.y=-.14
  tube('Seat back sewn welt',[(4.84,y+s*.264,.78),(4.84,y+s*.264,1.1),(4.78,y+s*.264,1.47)],.0025,labelmat)
  tube('Seat pan sewn welt',[(4.92,y+s*.266,.742),(5.25,y+s*.266,.748),(5.67,y+s*.266,.738)],.0025,labelmat)
  cyl('Seat support tube',(4.76,y+s*.36,.37),(4.63,y+s*.36,1.66),.022,alloy)
  box('Padded armrest',(5.2,y+s*.37,.97),(.55,.09,.13),rubber,.025)
  strap=box('Shoulder restraint strap',(4.898,y+s*.16,1.115),(.008,.048,.66),rubber,.003);strap.rotation_euler.y=-.14
  webbing('Shoulder strap buckle connection',(4.944,y+s*.16,.79),(5.17,y+s*.029,.794))
  webbing('Lap restraint',(5.2,y+s*.30,.754),(5.17,y+s*.028,.79))
  box('Harness length adjuster',(4.94,y+s*.16,.96),(.018,.064,.047),alloy,.005)
 box('Five point central buckle',(5.17,y,.78),(.09,.10,.036),alloy,.012)
 box('Buckle release',(5.17,y,.805),(.041,.059,.017),amber,.006)
 for xx in (4.83,5.45):
  for sy in (-.25,.25):fastener((xx,y+sy,.596))

for x in (4.53,5.,5.47,5.94,6.41,6.88,7.35,7.82):
 for y in (-1.12,-.55,0,.55,1.12):box('Non slip floor pad',(x,y,.167),(.39,.42,.016),rubber,.005)
box('Rear service door',(4.279,0,1.03),(.029,.70,1.38),ivory,.025)
box('Rear door pull',(4.32,.23,1.05),(.071,.055,.25),amber,.01)
for y in (-1.19,1.19):
 box('Emergency stowage',(4.32,y,1.16),(.25,.43,.70),ivory,.025)
 box('Stowage latch',(4.46,y,1.17),(.03,.22,.06),alloy,.007)
box('Overhead console',(4.91,0,1.97),(1.58,.70,.12),dark,.02)
for x in (4.40,4.70,5.00,5.30):
 for y in (-.18,.18):box('Overhead switch housing',(x,y,1.89),(.12,.1,.08),alloy,.006)
for y in (-.28,.28):
 o=box('Rudder pedal',(6.53,y,.34),(.21,.20,.055),alloy,.012);o.rotation_euler.y=-.4
 for yy in (-.066,0,.066):box('Pedal tread',(6.54,y+yy,.375),(.13,.012,.018),rubber,.002)

# A measured crop of NASA blanket, mapped only onto insulation faces. Source intact.
texpath=OUT/'Textures/OrionInsulation_BaseColor.png'
if texpath.exists():
 im=bpy.data.images.load(str(texpath));im.colorspace_settings.name='sRGB'
 nt=foil.node_tree;tex=nt.nodes.new('ShaderNodeTexImage');tex.image=im
 nt.links.new(tex.outputs['Color'],nt.nodes['Principled BSDF'].inputs['Base Color'])
 specs[foil.name]['base_color_texture']='Textures/'+texpath.name
 specs[foil.name]['photo_evidence']='NASA jsc2022e044970, gold insulation patch; lighting remains baked, roughness inferred'

# Convert and bake per authored part; parts retain origin zero and full ship coordinates.
final=[];manifest=[]
for name,objects in parts.items():
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.convert(target='MESH')
 bpy.ops.object.join();o=bpy.context.object;o.name=name;o['recon_part']=name.lower().replace('_','-')
 scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 if not o.data.uv_layers:o.data.uv_layers.new(name='UVMap')
 uv=o.data.uv_layers.active
 # Metric box mapping for manufactured surfaces; normalize each separate cushion later.
 for p in o.data.polygons:
  k=max(range(3),key=lambda a:abs(p.normal[a]));ij=[a for a in range(3) if a!=k]
  for li in p.loop_indices:
   co=o.data.vertices[o.data.loops[li].vertex_index].co
   if o.data.materials[p.material_index]==foil:
    uv.data[li].uv=((co.x-5.62)/.24,(co.z-.376)/.218)
   else:uv.data[li].uv=(co[ij[0]],co[ij[1]])
 o.data.calc_loop_triangles()
 manifest.append({'name':name,'fbx':'Parts/'+name+'.fbx','location_m':[0,0,0],'location_ue_cm':[0,0,0],'rotation_ue_deg':[0,0,0],'scale':[1,1,1],'triangles':len(o.data.loop_triangles),'materials':[m.name for m in o.data.materials]})
 bpy.ops.export_scene.fbx(filepath=str(OUT/'Parts'/f'{name}.fbx'),use_selection=True,axis_forward='X',axis_up='Z',global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',object_types={'MESH'},bake_anim=False,path_mode='RELATIVE',use_mesh_modifiers=True)
 final.append(o)
bpy.ops.object.select_all(action='DESELECT')
for o in final:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'STAR_CockpitV3.glb'),export_format='GLB',use_selection=True,export_apply=True)
metadata={'version':3,'status':'candidate_not_game_verified','source_units':'meters','source_frame':{'forward':'+X','right':'+Y','up':'+Z'},'fbx_axis_forward':'X','fbx_axis_up':'Z','fbx_apply_unit_scale':True,'fbx_scale_options':'FBX_SCALE_UNITS','import_uniform_scale':1,'model_parts':manifest,'materials':specs,'replace':{'SM_CockpitInterior':'SM_CockpitV3Interior','SM_InstrumentPanel':'SM_CockpitV3InstrumentPanel'},'retain':['SM_CockpitShell','SM_CanopyGlass','SM_ControlStick','SM_Throttle'],'camera_m':[6,0,1.3],'display_sockets':[{'location_m':[6.838,y,.747],'normal':[-1,0,0],'size_m':[.619,.312]} for y in (-.88,0,.88)],'glass_contract':{'base_color_linear':[.995,.995,.995],'opacity':0,'two_sided':False,'action':'do not edit glass material'},'new_geometry_original':True,'scientific_observation':False}
(ART/'manifest.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'Data/cockpit_v3_manifest.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf-8')

# Context shell is linked from original source only after exports, never delivered twice.
with bpy.data.libraries.load(str(ROOT/'Art/Explorer/V2/STAR_Explorer_V2.blend'),link=False) as (src,dst):
 dst.objects=[n for n in src.objects if n in ('SM_CockpitShell','SM_ControlStick','SM_Throttle')]
for o in dst.objects:
 if o:bpy.context.collection.objects.link(o);o['context_only']=True
def area(name,loc,target,power,size,color):
 d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=color
 o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
area('Exterior daylight',(7,-1,3.7),(6.3,0,.6),320,3,(.76,.85,1))
area('Cabin bounce',(5.7,.4,1.93),(6.5,0,.7),50,1.2,(1,.88,.72))
area('Console reading',(6.3,-.65,1.75),(6.9,0,.8),15,.7,(.85,.93,1))
camdata=bpy.data.cameras.new('ReviewCamera');cam=bpy.data.objects.new('ReviewCamera',camdata);bpy.context.collection.objects.link(cam);scene.camera=cam
scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
views=[('front',(6,0,1.3),(8,0,1.17),18,1600,1000),('side',(6.5,.7,1.65),(6.2,-1.2,.93),25,1400,1000),('rear',(7.45,0,1.62),(4.85,0,1),22,1400,1000),('holdout',(5.82,-.27,1.47),(7,1.19,1),26,1400,1000)]
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(ART/'STAR_CockpitV3.blend'))
receipts=[]
if '--no-render' not in sys.argv:
 for name,loc,target,lens,w,h in views:
  cam.location=loc;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();camdata.lens=lens
  scene.render.resolution_x=w;scene.render.resolution_y=h;scene.render.filepath=str(ART/'Renders'/f'{name}.png')
  bpy.ops.render.render(write_still=True)
  receipts.append({'view':name,'camera_m':loc,'target_m':target,'lens_mm':lens,'file':scene.render.filepath,'sha256':hashlib.sha256(Path(scene.render.filepath).read_bytes()).hexdigest()})
  (ART/'render_receipts.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
print('COCKPIT_V3_DONE',sum(p['triangles'] for p in manifest),flush=True)
