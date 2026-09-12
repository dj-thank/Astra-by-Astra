"""Prepare observed maps and deterministic 3-D curves for the UE component."""
from pathlib import Path
import argparse,json,hashlib,shutil
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt
p=argparse.ArgumentParser();p.add_argument('observatory',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2];dest=root/'Data/SolarMotion';dest.mkdir(parents=True,exist_ok=True)
for name in ['sun-304.jpg','sun-white.jpg','quality-white.png']:shutil.copy2(a.observatory/'textures'/name,dest/name)
mask=np.array(Image.open(dest/'quality-white.png'))
mask[:,:,2]=np.clip(distance_transform_edt(mask[:,:,2]>127)/96.0*255,0,255).astype(np.uint8)
Image.fromarray(mask).save(dest/'quality-white.png')
d=json.loads((a.observatory/'data/magnetic-model.json').read_text(encoding='utf-8'))
selected=sorted([c for c in d['curves'] if not c['open'] and 1.014<c['maxRadius']<1.5],key=lambda c:-abs(c['seedB']))[:96]
curves=[]
for c in selected:
    p=np.array(c['points']);p=np.stack([-p[:,0],-p[:,2],p[:,1]],1) # renderer -> Carrington UE
    s=np.r_[0,np.cumsum(np.linalg.norm(np.diff(p,axis=0),axis=1))];t=np.linspace(0,s[-1],48)
    curves.append(np.stack([np.interp(t,s,p[:,k]) for k in range(3)],1).round(7).tolist())
assert len(curves)==96
rng=np.random.default_rng(9231)
for i in range(320):
    z=rng.uniform(-.93,.93);phi=rng.uniform(0,2*np.pi);n=np.array([np.sqrt(1-z*z)*np.cos(phi),np.sqrt(1-z*z)*np.sin(phi),z])
    v=np.cross(n,[0,0,1]);v/=np.linalg.norm(v);height=rng.uniform(.006,.027)
    points=[(n*(1+height*t)+v*.003*np.sin(t*np.pi)).tolist() for t in np.linspace(0,1,9)]
    curves.append(points)
out=root/'Content/Star/Data/solar-motion-curves.json';out.write_text(json.dumps({'curves':curves},separators=(',',':')),encoding='utf-8')
shutil.copy2(out,dest/'curves.json')
manifest={'imageProvenance':json.loads((a.observatory/'data/reconstruction.json').read_text()),'fieldProvenance':{k:v for k,v in d.items() if k!='curves'},'model':'96 PFSS arches + 320 synthetic spicules; UTC-periodic artistic flow, opacity and displacement; close disk display compression .012','frame':'UE local [Carrington X,-Carrington Y,North Z], radius=1','curvesSha256':hashlib.sha256(out.read_bytes()).hexdigest(),'assets':{f:hashlib.sha256((dest/f).read_bytes()).hexdigest() for f in ['sun-304.jpg','sun-white.jpg','quality-white.png']}}
(dest/'provenance.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
shutil.copy2(dest/'provenance.json',root/'Content/Star/Data/solar-motion-provenance.json')
print('Prepared',len(curves),'curves')
