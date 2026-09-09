import bpy,json,os,time
import numpy as np
from pathlib import Path
from mathutils import Matrix
root=Path.cwd();meta=json.loads((root/'Content/Star/Data/apollo17_far.json').read_text());reg=json.loads((root/'Data/lunar_panorama_full_region_registration.json').read_text());out=root/'Content/Star/Art/LunarPanorama/FullRegion/Review';out.mkdir(exist_ok=True);print('PID',os.getpid(),'start',time.time(),flush=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
H=np.fromfile(root/meta['binaryPath'],dtype='<i2').reshape(meta['height'],meta['width']);valid=np.fromfile(root/meta['confidencePath'],dtype='u1').reshape(H.shape);ys=np.arange(0,meta['height'],8);xs=np.arange(0,meta['width'],8);C,R=np.meshgrid(xs,ys);z=H[R,C].astype(np.float64)*.25;V=valid[R,C]>0;lat=np.deg2rad(meta['northLatitudeDegrees']-(R+.5)/meta['height']*(meta['northLatitudeDegrees']-meta['southLatitudeDegrees']));lon=np.deg2rad(meta['westLongitudeDegrees']+(C+.5)/meta['width']*(meta['eastLongitudeDegrees']-meta['westLongitudeDegrees']));lat0,lon0=np.deg2rad([20.1908,30.7717]);rad=1737400.;r=rad+z;dl=lon-lon0;P=np.stack([r*np.cos(lat)*np.sin(dl),r*(np.sin(lat)*np.cos(lat0)-np.cos(lat)*np.sin(lat0)*np.cos(dl)),r*(np.sin(lat)*np.sin(lat0)+np.cos(lat)*np.cos(lat0)*np.cos(dl))-(rad-2626.984130859375)],axis=-1);uv=np.stack([(C+.5)/meta['width'],1-(R+.5)/meta['height']],axis=-1).reshape(-1,2);faces=[];nw=len(xs)
# Preview the same far/LOLA fallback and legacy near blend as the runtime catalog.
G=np.fromfile(root/'Content/Star/Data/moon_ldem_16_i16.bin',dtype='<i2').reshape(2880,5760)
def bilinear(arr,x,y,wrap=False):
 x0=np.floor(x).astype(int);y0=np.floor(y).astype(int);fx=x-x0;fy=y-y0;y0=np.clip(y0,0,arr.shape[0]-1);y1=np.clip(y0+1,0,arr.shape[0]-1)
 xa=x0%arr.shape[1] if wrap else np.clip(x0,0,arr.shape[1]-1);xb=(x0+1)%arr.shape[1] if wrap else np.clip(x0+1,0,arr.shape[1]-1)
 return (arr[y0,xa].astype(float)*(1-fx)+arr[y0,xb].astype(float)*fx)*(1-fy)+(arr[y1,xa].astype(float)*(1-fx)+arr[y1,xb].astype(float)*fx)*fy
def smooth(x):
 x=np.clip(x,0,1);return x*x*(3-2*x)
ld=np.rad2deg(lon);pd=np.rad2deg(lat);globalz=bilinear(G,((ld+180)%360)/360*5760-.5,np.clip((90-pd)/180*2880-.5,0,2879),True)*.5
fm=smooth((valid[R,C].astype(float)/255*500-20)/480);nm=json.loads((root/'Content/Star/Data/apollo17.json').read_text());nx=(ld-nm['westLongitudeDegrees'])/(nm['eastLongitudeDegrees']-nm['westLongitudeDegrees'])*2800-.5;ny=(nm['northLatitudeDegrees']-pd)/(nm['northLatitudeDegrees']-nm['southLatitudeDegrees'])*2400-.5;inside=(ld>=nm['westLongitudeDegrees'])&(ld<=nm['eastLongitudeDegrees'])&(pd>=nm['southLatitudeDegrees'])&(pd<=nm['northLatitudeDegrees']);eg=np.maximum(np.maximum(nm['westLongitudeDegrees']-ld,ld-nm['eastLongitudeDegrees']),0);ng=np.maximum(np.maximum(nm['southLatitudeDegrees']-pd,pd-nm['northLatitudeDegrees']),0);outside=np.hypot(np.deg2rad(eg)*rad*np.cos(np.deg2rad(20)),np.deg2rad(ng)*rad);z=globalz+(z-globalz)*fm
near=np.fromfile(root/'Content/Star/Data/apollo17_height_f32.bin',dtype='<f4').reshape(2400,2800);nearz=bilinear(near,nx,ny);edge=np.minimum(np.minimum(nx,2799-nx),np.minimum(ny,2399-ny));nz=z+(nearz-z)*smooth(edge/60);available=(nx>=0)&(nx<2799)&(ny>=0)&(ny<2399);z=np.where(inside,np.where(available,nz,z),z);r=rad+z;P=np.stack([r*np.cos(lat)*np.sin(dl),r*(np.sin(lat)*np.cos(lat0)-np.cos(lat)*np.sin(lat0)*np.cos(dl)),r*(np.sin(lat)*np.sin(lat0)+np.cos(lat)*np.cos(lat0)*np.cos(dl))-(rad-2626.984130859375)],axis=-1);V[:]=True
for y in range(len(ys)-1):
 for x in range(nw-1):
  if not np.all(V[y:y+2,x:x+2]):continue
  i=y*nw+x;j=i+nw;faces.extend([(i,j,i+1),(i+1,j,j+1)])
mesh=bpy.data.meshes.new('Apollo17FullMeasuredFar');mesh.from_pydata(P.reshape(-1,3).tolist(),[],faces);mesh.update();obj=bpy.data.objects.new('Apollo17FullMeasuredFar',mesh);bpy.context.collection.objects.link(obj);obj['recon_part']='full-measured-far-dtm';uvlayer=mesh.uv_layers.new(name='GeographicUV')
uvdata=np.array([uv[l.vertex_index] for l in mesh.loops],dtype=np.float32).reshape(-1);uvlayer.data.foreach_set('uv',uvdata)
mat=bpy.data.materials.new('PhotoReferenceOnMeasuredDTM');mat.use_nodes=True;n=mat.node_tree.nodes;n.clear();tx=n.new('ShaderNodeTexImage');tx.image=bpy.data.images.load(str(root/'work/lunar-panorama/full-region/preview_texture.png'));tx.extension='EXTEND';em=n.new('ShaderNodeEmission');output=n.new('ShaderNodeOutputMaterial');mat.node_tree.links.new(tx.outputs['Color'],em.inputs['Color']);mat.node_tree.links.new(em.outputs[0],output.inputs['Surface']);obj.data.materials.append(mat)
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.device='CPU';s.cycles.samples=1;s.cycles.use_denoising=False;s.render.threads_mode='FIXED';s.render.threads=4;s.render.resolution_x=1170;s.render.resolution_y=1175;s.render.resolution_percentage=100;s.world.use_nodes=True;s.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(0,0,0,1);s.view_settings.view_transform='Standard';s.view_settings.look='None';s.view_settings.exposure=0;s.view_settings.gamma=1
camdata=bpy.data.cameras.new('RegisteredCamera');cam=bpy.data.objects.new('RegisteredCamera',camdata);s.collection.objects.link(cam);camdata.sensor_fit='HORIZONTAL';camdata.sensor_width=36;camdata.clip_end=60000;camdata.clip_start=1;s.camera=cam;receipt=[]
def render(name,K,R,pos):
 camdata.lens=K[0][0]*36/1170;camdata.shift_x=(585-K[0][2])/1170;camdata.shift_y=(K[1][2]-587.5)/1170;cam.matrix_world=(Matrix(R)@Matrix(((1,0,0),(0,-1,0),(0,0,-1)))).to_4x4();cam.location=pos;s.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True);receipt.append(dict(name=name,positionEnuMeters=pos,cameraToWorld=[list(row) for row in cam.matrix_world],focalMillimeters=camdata.lens))
for c in reg['cameras']:
 render(c['direction']+'_reference',c['K'],c['cameraToEastNorthUp'],[0,0,1.6]);h=c['independentPhotoHoldout'];render(c['direction']+'_independent',h['K'],h['cameraToEastNorthUp'],[0,0,1.6])
 if c['direction'] in ['southwest','southeast','west']:render(c['direction']+'_east100m',c['K'],c['cameraToEastNorthUp'],[100,0,1.6])
(root/'work/lunar-panorama/full-region/cpu_render_receipt.json').write_text(json.dumps(dict(status='CPU_BLENDER_ONLY',previewSpacingMeters=80,previewTexturePixels=[4096,4096],runtimeSourceSpacingMeters=10,vertices=len(mesh.vertices),triangles=len(faces),views=receipt),indent=2),encoding='utf-8');print('DONE',flush=True)
