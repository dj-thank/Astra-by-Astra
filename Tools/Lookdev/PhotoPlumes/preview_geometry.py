"""CPU orthographic additive raster of the delivered mesh; not UE validation."""
from pathlib import Path
import json, math, hashlib
import numpy as np
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[3]
ART=ROOT/'Content/Star/Art/PhotoPlumes'
HERE=Path(__file__).resolve().parent

def smooth(a,b,x):
    t=np.clip((x-a)/(b-a),0,1); return t*t*(3-2*t)

def disks():
    vertices=[]; uv=[]; faces=[]
    for layer in range(8):
        t=.035+layer*.12
        radius=.5*(1+.35*t)*(1-.8*smooth(.8,1,t))
        base=len(vertices); vertices.append([8*t,0,0]); uv.append([0,t])
        for seg in range(16):
            a=2*math.pi*seg/16
            vertices.append([8*t,radius*math.cos(a),radius*math.sin(a)])
            uv.append([1,t])
        for seg in range(16): faces.append([base,base+1+seg,base+1+(seg+1)%16])
    mesh={'units':'nozzle_diameter_D','local_exhaust_axis':'+X','vertices':vertices,'uv0':uv,'triangles':faces,'uv_contract':'U radial 0=center 1=edge; V axial position','interpretation':'inferred transverse emissivity slices, not measured tomography'}
    (ART/'plume_disks.json').write_text(json.dumps(mesh,separators=(',',':')),encoding='utf-8')
    return mesh

def render(mesh, angle, tex, disk=False):
    w,h=800,280
    theta=math.radians(angle)
    # angle=0 side, 90 exactly down exhaust axis. Same fixed scale in all views.
    p=np.array(mesh['vertices']); uv=np.array(mesh['uv0'])
    projected=np.stack([p[:,0]*math.cos(theta)-p[:,1]*math.sin(theta),p[:,2]],axis=1)
    projected[:,0]=(projected[:,0]-4*math.cos(theta))*85+w/2
    projected[:,1]=projected[:,1]*85+h/2
    target=np.zeros((h,w,3))
    for face in mesh['triangles']:
        pts=projected[face]; coords=uv[face]
        lo=np.maximum(np.floor(pts.min(axis=0)).astype(int),0)
        hi=np.minimum(np.ceil(pts.max(axis=0)).astype(int),[w-1,h-1])
        if (hi<lo).any(): continue
        a,b,c=pts
        den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-8: continue
        yy,xx=np.mgrid[lo[1]:hi[1]+1,lo[0]:hi[0]+1]; xx=xx+.5; yy=yy+.5
        ba=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
        bb=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
        bc=1-ba-bb; mask=(ba>=0)&(bb>=0)&(bc>=0)
        q=ba[...,None]*coords[0]+bb[...,None]*coords[1]+bc[...,None]*coords[2]
        ix=np.clip((q[:,:,0]*(tex.shape[1]-1)).astype(int),0,tex.shape[1]-1)
        iy=np.clip((q[:,:,1]*(tex.shape[0]-1)).astype(int),0,tex.shape[0]-1)
        sample=tex[iy,ix]
        if disk:
            # Exact proposed axial custom code: radial falloff times mean profile.
            alpha=sample[:,:,0]*(1-smooth(.05,1,q[:,:,0]))*.025
            color=np.array([.55,.7,1.0])
            signal=alpha[...,None]*color
        else:
            signal=sample[:,:,:3]*(.9+.1*np.array([.25,.65,1]))*sample[:,:,3,None]*(1-smooth(.94,1,q[:,:,1]))[...,None]/6
        target[lo[1]:hi[1]+1,lo[0]:hi[0]+1]+=signal*mask[...,None]
    return target

def main():
    mesh=json.loads((ART/'plume_mesh.json').read_text(encoding='utf-8'))
    disk=disks()
    photo=np.asarray(Image.open(ART/'T_PlumePhoto.png'),dtype=float)/255
    axial=np.asarray(Image.open(ART/'T_PlumeAxial.png'),dtype=float)/255
    result=Image.new('RGB',(800,4*310),(7,9,15)); d=ImageDraw.Draw(result)
    for i,angle in enumerate((0,45,80,90)):
        signal=render(mesh,angle,photo)+render(disk,angle,axial,True)
        assert np.isfinite(signal).all() and signal.max()>0
        display=np.power(1-np.exp(-signal*3),1/2.2)
        result.paste(Image.fromarray(np.uint8(display*255)),(0,i*310+30))
        d.text((10,i*310+8),f'CPU geometry / angle {angle} deg (90 = rear) / fixed scale + exposure / no bloom',fill='white')
    result.save(HERE/'geometry_views.png')
    manifest_path=ROOT/'Data/photo_plumes_sources.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    p=ART/'plume_disks.json'
    manifest['files'][p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
    manifest['validation']={'cpu_views_degrees':[0,45,80,90],'cpu_shader_signal_spool':[.18,.55,1],'engine_shader_compilation':'NOT_RUN','gpu_timing':'NOT_RUN','independent_visual_review':'NOT_RUN','residuals':['crossed-sheet angular rays at oblique near-rear views','static photograph has no measured motion','source exposure does not calibrate radiance']}
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS: 4 CPU geometry views including rear are finite and nonzero; UE/GPU pending.')

if __name__=='__main__': main()
