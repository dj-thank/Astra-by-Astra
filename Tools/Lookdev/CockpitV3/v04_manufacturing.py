# v0.4 manufacturing additions. Geometry is an original engineering inference.
MANUFACTURING_CHANGES = {
 'cooling': 'Replace 12 mm raised bars with 1.6 mm folded louvres, 8 mm shadow depth, continuous supported edge flanges',
 'display': 'Separate 2 mm radiused machined bezel, folded return, sealing joint, fixing flange and phenolic legend strip',
 'controls': 'Distinct phenolic actuator caps, inset metal retainers, removable front and circuit-breaker plates',
 'liners': '2.5 mm chamfered folded sheet with stiffening beads replaces 22 mm rectangular blocks',
 'surface': 'Manufacturing normals have explicit metric pitch and authored amplitude. No base-color brightness compensation'
}
phenolic=mat('M_CV4_ControlPhenolic',(.029,.036,.039),0,.46)
polymer=mat('M_CV4_TexturedPolymer',(.025,.031,.033),0,.66)
# Swatches cover 0.10 m and contain no photographed lighting or dirt.
for m,key,strength in ((ivory,'Powder',.28),(alloy,'Machined',.16),(dark,'Anodized',.20),(phenolic,'Phenolic',.24),(polymer,'Polymer',.32)):
 nt=m.node_tree;p=nt.nodes.get('Principled BSDF')
 uv=nt.nodes.new('ShaderNodeTexCoord');scale=nt.nodes.new('ShaderNodeVectorMath');scale.operation='SCALE';scale.inputs[3].default_value=10
 nt.links.new(uv.outputs['UV'],scale.inputs[0])
 for suffix,target in (('Roughness','Roughness'),('NormalGL','Normal')):
  fn=f'Textures/V04_{key}_{suffix}.png';tex=nt.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(OUT/fn));tex.image.colorspace_settings.name='Non-Color'
  nt.links.new(scale.outputs[0],tex.inputs['Vector'])
  if target=='Normal':
   normal=nt.nodes.new('ShaderNodeNormalMap');normal.inputs['Strength'].default_value=strength
   nt.links.new(tex.outputs['Color'],normal.inputs['Color']);nt.links.new(normal.outputs['Normal'],p.inputs['Normal'])
   specs[m.name].update(normal_texture=fn,normal_strength=strength,normal_uv_repeat=10)
  else:
   nt.links.new(tex.outputs['Color'],p.inputs['Roughness']);specs[m.name].update(roughness_texture=fn,roughness_uv_repeat=10)
 specs[m.name]['source_kind']='synthetic manufacturing finish, not a NASA observation'

def vent_louvres(y):
 # The underside is 8 mm below the sheet; slots remain geometrically open.
 box('Cooling duct inner shadow',(7.106,y,1.001),(.252,.462,.010),rubber,.002)
 for xx in (6.975,7.237):box('Louvre end flange',(xx,y,1.008),(.010,.477,.002),dark,.0006)
 for yy in (y-.233,y+.233):box('Louvre side flange',(7.106,yy,1.008),(.272,.012,.002),dark,.0006)
 for i in range(9):
  xx=6.994+i*.028
  o=box('Stamped cooling louvre',(xx,y,1.013),(.020,.452,.0016),dark,.0004);o.rotation_euler.y=math.radians(24)
  # The rolled lip catches a narrow physical highlight instead of a thick bar.
  box('Louvre rolled trailing edge',(xx+.009,y,1.009),(.0018,.452,.0018),dark,.0005)
 for xx in (6.978,7.235):
  for yy in (y-.23,y+.23):fastener((xx,yy,1.009))

def liner_panel(x,y,height,side):
 import bmesh
 z=.97;half=.28;chamfer=.05 if height<.23 else .035;thick=.0025
 outline=[(x-half,z),(x+half,z),(x+half,z+height-chamfer),(x+half-chamfer,z+height),(x-half+chamfer,z+height),(x-half,z+height-chamfer)]
 verts=[(xx,y+dy,zz) for dy in (-thick/2,thick/2) for xx,zz in outline];n=len(outline)
 faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 me=bpy.data.meshes.new('Folded pressure liner');me.from_pydata(verts,[],faces);me.update()
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free()
 o=bpy.data.objects.new('Chamfered pressure liner sheet',me);bpy.context.collection.objects.link(o);reg(o,o.name,ivory,.0008)
 for xx in (x-half+.003,x+half-.003):box('Pressure liner folded edge return',(xx,y+side*.006,z+(height-chamfer)/2),(.005,.014,height-chamfer),ivory,.001)
 box('Pressure liner lower return',(x,y+side*.006,z+.003),(.55,.014,.006),ivory,.001)
 # Broad pressed stiffening beads explain the large clean sheet surface.
 for zz in (z+.044,z+height-.061):box('Pressed liner stiffening bead',(x,y-side*.003,zz),(.422,.005,.012),ivory,.003)
