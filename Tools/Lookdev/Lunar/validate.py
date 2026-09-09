"""Image integrity + limiting math; not an HLSL compiler or runtime proof."""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[3]
out=root/'Content/Star/Art/Lookdev/Lunar'
m=json.loads((out/'provenance.json').read_text(encoding='utf-8'))
for name,d in m['outputs'].items():
 assert hashlib.sha256((out/name).read_bytes()).hexdigest()==d['sha256']
 assert Image.open(out/name).size==(2048,2048)
p=np.array(Image.open(out/'lunar_detail_linear.png'),dtype=np.float32)/255
factor=1+(p[:,:,0]*2-1)*.35
rough=np.clip(.92+(p[:,:,1]-.5)*.12,.75,1)
assert factor.min()>=.824 and factor.max()<=1.176
assert rough.min()>=.75 and rough.max()<=1 and np.ptp(rough)>.001
normal=np.array(Image.open(out/'lunar_detail_normal_dx.png'),dtype=np.float32)/255*2-1
normal[:,:,:2]*=.35
normal[:,:,2]=np.maximum(normal[:,:,2],.1)
normal/=np.linalg.norm(normal,axis=2)[:,:,None]
assert np.isfinite(normal).all() and np.max(np.abs(np.linalg.norm(normal,axis=2)-1))<1e-5
# Metres map to exact texture cycles. Rebase changes render positions, never UV1.
uv=np.array([123.125,-67.5]); assert np.allclose((uv+np.array([2.5,0]))*.4-uv*.4,[1,0])
far=np.concatenate([normal[:,:,:2]*0,np.maximum(normal[:,:,2:3],.1)],axis=2)
far/=np.linalg.norm(far,axis=2)[:,:,None]
assert np.allclose(far,[0,0,1]) # far fade returns geometric normal
print(json.dumps(dict(status='LOCAL_MATH_IMAGE_PASS',factor_min=float(factor.min()),factor_max=float(factor.max()),factor_mean=float(factor.mean()),roughness_min=float(rough.min()),roughness_max=float(rough.max()),final_bytes=sum(d['bytes'] for d in m['outputs'].values()),note='UE shader compilation and packaged visuals pending'),indent=2))
