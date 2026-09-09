"""CPU-only same camera, same lighting A/B. Static offline screens are deliberately blank."""
from pathlib import Path
import bpy,json,math,hashlib,os,sys
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3];CONTEXT=Path(os.environ.get('STAR_CONTEXT_ROOT',str(ROOT)));ART=ROOT/'work/cockpit-v04';OUT=ROOT/'Content/Star/Art/CockpitV3/ReviewV04'
OUT.mkdir(parents=True,exist_ok=True)
views=[('front',(6,0,1.3),(6+math.cos(math.radians(12)),0,1.3-math.sin(math.radians(12))),100),('left',(6.4,.5,1.6),(5.95,-1.20,.91),75),('right',(6.4,-.5,1.6),(5.95,1.20,.91),75),('rear',(7.32,0,1.51),(4.75,0,1.10),83)]
selected=sys.argv[sys.argv.index('--state')+1] if '--state' in sys.argv else 'all'
receipt_path=OUT/'render_receipts.json'
receipts=[r for r in json.loads(receipt_path.read_text(encoding='utf-8')) if r['state']!=selected] if selected!='all' and receipt_path.exists() else []
for state,blend in [('baseline',CONTEXT/'Art/Explorer/CockpitV3/STAR_CockpitV3.blend'),('candidate',ART/'STAR_CockpitV3.blend')]:
 if selected!='all' and state!=selected:continue
 bpy.ops.wm.open_mainfile(filepath=str(blend));scene=bpy.context.scene
 if not bpy.data.objects.get('SM_CockpitV3SideDetails'):
  with bpy.data.libraries.load(str(CONTEXT/'Art/Explorer/CockpitV3/STAR_CockpitV3SideDetails.blend'),link=False) as (src,dst):dst.objects=[n for n in src.objects if n=='SM_CockpitV3SideDetails']
  for o in dst.objects:
   if o:bpy.context.collection.objects.link(o)
 scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=16;scene.cycles.use_denoising=True;scene.render.threads_mode='FIXED';scene.render.threads=4
 scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100;scene.view_settings.view_transform='AgX';scene.view_settings.exposure=0
 cam=scene.camera;cam.data.type='PERSP';cam.data.sensor_fit='HORIZONTAL';cam.data.sensor_width=36
 for name,loc,target,fov in views:
  cam.location=loc;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.lens=36/(2*math.tan(math.radians(fov/2)))
  path=OUT/f'{state}_{name}.png';scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)
  receipts.append({'state':state,'view':name,'camera_m':list(loc),'target_m':list(target),'horizontal_fov_deg':fov,'resolution':[1600,900],'color_transform':'AgX; exposure 0','lighting':'unchanged v03 CPU model studio','renderer':'Cycles CPU4 16 samples denoised','file':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
  (OUT/'render_receipts.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
print('OFFLINE_PREVIEW_DONE',flush=True)
