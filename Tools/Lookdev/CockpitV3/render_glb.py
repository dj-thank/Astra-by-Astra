"""Same side camera and studio, replacing authoring geometry with actual GLB readback."""
from pathlib import Path
import bpy,json,hashlib
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3];ART=ROOT/'Art/Explorer/CockpitV3';OUT=ROOT/'Content/Star/Art/CockpitV3'
bpy.ops.wm.open_mainfile(filepath=str(ART/'STAR_CockpitV3.blend'))
for name in ('SM_CockpitV3Interior','SM_CockpitV3InstrumentPanel'):
 o=bpy.data.objects.get(name)
 if o:bpy.data.objects.remove(o,do_unlink=True)
bpy.ops.import_scene.gltf(filepath=str(OUT/'STAR_CockpitV3.glb'))
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24
scene.render.threads_mode='FIXED';scene.render.threads=4
cam=scene.camera;cam.location=(6.5,.7,1.65);cam.rotation_euler=(Vector((6.2,-1.2,.93))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.lens=25
scene.render.resolution_x=1400;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
path=ART/'Renders/glb_side.png';scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)
(ART/'glb_render_receipt.json').write_text(json.dumps({'glb_sha256':hashlib.sha256((OUT/'STAR_CockpitV3.glb').read_bytes()).hexdigest(),'image_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'camera_m':list(cam.location),'camera_matrix':[list(row) for row in cam.matrix_world],'comparison':'Renders/side.png: identical camera, studio and context; actual exported GLB replaces both candidate meshes','status':'AWAITING_VISUAL_COMPARE'},indent=2),encoding='utf-8')
