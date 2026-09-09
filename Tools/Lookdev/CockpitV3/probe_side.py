from pathlib import Path
import bpy,json,math
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Art/Explorer/CockpitV3/STAR_CockpitV3.blend'))
shell=bpy.data.objects['SM_CockpitShell']
rows=[]
for p in shell.data.polygons:
 if p.area>.25:
  rows.append({'area':p.area,'normal':list(p.normal),'material':shell.data.materials[p.material_index].name,'vertices':[list(shell.matrix_world@shell.data.vertices[i].co) for i in p.vertices]})
cam=Vector((6,0,1.3));rays=[]
for px,py in [(50,1000),(350,1100),(700,1200),(900,1300),(3400,1100)]:
 d=Vector((1,-(2*px/3840-1)*math.tan(math.radians(50)),-(2*py/2160-1)*math.tan(math.radians(50))/(3840/2160))).normalized()
 hit,loc,n,idx=shell.ray_cast(cam,d)
 rays.append({'pixel':[px,py],'hit':hit,'point':list(loc),'normal':list(n),'face':idx})
(ROOT/'Art/Explorer/CockpitV3/side_probe.json').write_text(json.dumps({'large_faces':rows,'rays':rays},indent=2),encoding='utf-8')
print(json.dumps(rays),flush=True)
