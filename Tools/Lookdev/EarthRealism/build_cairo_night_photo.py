"""Reproduce the registered observational texture from retained source and map fit."""
from pathlib import Path
import argparse,hashlib,json,sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'work/scenic-flight/python'))
import cv2,numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'work/scenic-flight/rebuilt-cairo.png');a=p.parse_args()
m=json.loads((ROOT/'Data/earth_piloted_night_photo.json').read_text(encoding='utf-8'));r=m['registration']
source_file=ROOT/m['sourceFile'];assert hashlib.sha256(source_file.read_bytes()).hexdigest()==m['sourceSHA256']
source=np.array(Image.open(source_file).convert('RGB'));H=np.array(r['localTilePixelsToPanorama']);origin=np.array(r['tileOriginXY'])*256;world=256*2**r['zoom']
w,s,e,n=m['boundsWsen'];width,height=m['pixels'];out=np.zeros((height,width,4),np.uint8)
x=((w+(np.arange(width)+.5)/width*(e-w)+180)/360*world-origin[0])[None,:]
for y0 in range(0,height,128):
 lat=n-(np.arange(y0,min(height,y0+128))+.5)/height*(n-s)
 y=((1-np.arcsinh(np.tan(np.deg2rad(lat)))/np.pi)/2*world-origin[1])[:,None]
 den=H[2,0]*x+H[2,1]*y+H[2,2];u=(H[0,0]*x+H[0,1]*y+H[0,2])/den;v=(H[1,0]*x+H[1,1]*y+H[1,2])/den
 rgb=cv2.remap(source,u.astype(np.float32),v.astype(np.float32),cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT)
 edge=np.minimum(np.minimum(u,v),np.minimum(source.shape[1]-1-u,source.shape[0]-1-v))
 alpha=np.clip(edge/m.get('edgeFeatherSourcePixels',80),0,1);alpha=alpha*alpha*(3-2*alpha)
 out[y0:y0+len(lat),:,:3]=rgb;out[y0:y0+len(lat),:,3]=np.rint(alpha*255).astype(np.uint8)
a.output.parent.mkdir(parents=True,exist_ok=True);Image.fromarray(out).save(a.output)
digest=hashlib.sha256(a.output.read_bytes()).hexdigest();assert digest==m['sha256'],'Reproduction differs from the accepted texture bytes'
print('Exact texture reproduction:',digest)
