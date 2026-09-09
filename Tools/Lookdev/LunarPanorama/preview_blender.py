# Run from repository root; intermediate output belongs to work/lunar-panorama.
import bpy,sys,json,os,time
from pathlib import Path
from mathutils import Matrix,Vector
root=Path.cwd();out=root/'Content/Star/Art/LunarPanorama';cache=root/'work/lunar-panorama';reg=json.loads((cache/'registered.json').read_text());print('PROCESS',os.getpid(),time.time(),flush=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=str(out/'north_massif_measured_enu_m.obj'),forward_axis='Y',up_axis='Z')
obj=next(o for o in bpy.context.scene.objects if o.type=='MESH');obj['recon_part']='north-massif-measured-dtm';obj['source']='NASA/GSFC/ASU LROC 5m DTM';obj['photographic_status']='approximate registration candidate, original LM view only'
# OBJ importer keeps ENU X-east Y-north Z-up with explicit Y-forward import.
mat=bpy.data.materials.new('NorthMassif_PhotoReference_Unlit');mat.use_nodes=True;nodes=mat.node_tree.nodes;nodes.clear();tx=nodes.new('ShaderNodeTexImage');tx.image=bpy.data.images.load(str(out/'north_massif_projection_on_neutral.png'));tx.image.pack();tx.extension='EXTEND';em=nodes.new('ShaderNodeEmission');em.inputs['Strength'].default_value=1;output=nodes.new('ShaderNodeOutputMaterial');mat.node_tree.links.new(tx.outputs['Color'],em.inputs['Color']);mat.node_tree.links.new(em.outputs[0],output.inputs['Surface']);obj.data.materials.clear();obj.data.materials.append(mat)
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=1;scene.cycles.use_denoising=False;scene.render.threads_mode='FIXED';scene.render.threads=4;scene.render.resolution_x=1170;scene.render.resolution_y=1175;scene.render.resolution_percentage=100;scene.world.color=(0,0,0);scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(0,0,0,1);scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0;scene.view_settings.gamma=1
c=next(c for c in reg['refinedCameras'] if c['frame']==22500);K=c['K'];R=Matrix(c['cameraToEastNorthUp']);base=R@Matrix(((1,0,0),(0,-1,0),(0,0,-1)))
camdata=bpy.data.cameras.new('photo22500');cam=bpy.data.objects.new('photo22500',camdata);scene.collection.objects.link(cam);camdata.type='PERSP';camdata.sensor_fit='HORIZONTAL';camdata.sensor_width=36;camdata.lens=K[0][0]*36/1170;camdata.shift_x=(1170/2-K[0][2])/1170;camdata.shift_y=(K[1][2]-1175/2)/1170;camdata.clip_start=.1;camdata.clip_end=25000;cam.matrix_world=base.to_4x4();cam.location=(0,0,1.6);scene.camera=cam
outviews=out/'Review';outviews.mkdir(exist_ok=True)
views=[('reference', (0,0,1.6)),('east_100m',(100,0,1.6)),('west_100m',(-100,0,1.6)),('elevated_250m',(0,0,251.6))]
receipt=[]
for name,pos in views:
 cam.location=pos;scene.render.filepath=str(outviews/(name+'.png'));bpy.ops.render.render(write_still=True);receipt.append(dict(view=name,positionEnuMeters=pos,cameraToWorld=[list(row) for row in cam.matrix_world]))
cam.location=views[0][1];bpy.ops.wm.save_as_mainfile(filepath=str(out/'north_massif_candidate.blend'))
bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj;bpy.ops.export_scene.gltf(filepath=str(out/'north_massif_candidate.glb'),export_format='GLB',use_selection=True,export_yup=True)
(cache/'blender_review.json').write_text(json.dumps(dict(status='CPU_BLENDER_CANDIDATE_RENDER_ONLY',views=receipt,renderEngine=scene.render.engine,device=scene.cycles.device,vertices=len(obj.data.vertices),triangles=sum(len(p.vertices)-2 for p in obj.data.polygons)),indent=2),encoding='utf-8')

# Re-import the exported GLB in this dedicated headless scene.
bpy.data.objects.remove(obj,do_unlink=True)
bpy.ops.import_scene.gltf(filepath=str(out/'north_massif_candidate.glb'))
scene.render.filepath=str(outviews/'glb_readback.png');bpy.ops.render.render(write_still=True)
