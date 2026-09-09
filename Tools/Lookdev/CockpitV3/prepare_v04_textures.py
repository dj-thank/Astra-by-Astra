"""Deterministic metric manufacturing swatches. This is authored surface data, not photography."""
from pathlib import Path
import json, hashlib
import numpy as np
from PIL import Image, ImageFilter
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'Content/Star/Art/CockpitV3/Textures'
OUT.mkdir(parents=True,exist_ok=True)
rng=np.random.default_rng(40907);n=512;yy,xx=np.mgrid[:n,:n]
def noise(sigma):
 a=(rng.random((n,n))*255).astype('uint8')
 b=np.asarray(Image.fromarray(a).filter(ImageFilter.GaussianBlur(sigma)),dtype=np.float64)/255
 return (b-b.mean())/(b.std()+1e-9)
# Height is micrometers. Every swatch covers 100 mm; normals derive from physical gradients.
recipes={
 'Powder':{'mean':.58,'rough_amp':.018,'height_um':2.0,'height':noise(1.3)},
 'Machined':{'mean':.29,'rough_amp':.025,'height_um':.65,'height':np.sin(yy*2*np.pi/3.7)*.65+noise(.7)*.20},
 'Anodized':{'mean':.36,'rough_amp':.014,'height_um':.55,'height':noise(.8)},
 'Phenolic':{'mean':.46,'rough_amp':.020,'height_um':1.6,'height':noise(1.1)},
 'Polymer':{'mean':.66,'rough_amp':.026,'height_um':5.5,'height':noise(2.3)}
}
report={'source_kind':'synthetic manufacturing data','seed':40907,'physical_swatch_m':.1,'pixels':[n,n],'normal_convention':'OpenGL +Y','color_space':'linear scalar roughness and linear tangent normal; not sRGB','families':{}}
for name,r in recipes.items():
 h=r.pop('height');height=h*r['height_um']*1e-6;gx=(np.roll(height,-1,1)-np.roll(height,1,1))/(2*.1/n);gy=(np.roll(height,-1,0)-np.roll(height,1,0))/(2*.1/n)
 normal=np.dstack((-gx,-gy,np.ones_like(gx)));normal/=np.linalg.norm(normal,axis=2)[...,None]
 rough=np.clip(r['mean']+h*r['rough_amp'],.1,.95)
 maps={'NormalGL':np.round((normal*.5+.5)*255).astype('uint8'),'Roughness':np.round(rough*255).astype('uint8')}
 paths={}
 for suffix,a in maps.items():
  f=OUT/f'V04_{name}_{suffix}.png';Image.fromarray(a).save(f);paths[suffix]={'path':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
 report['families'][name]={**r,'roughness_min_max':[float(rough.min()),float(rough.max())],'maps':paths}
(OUT/'V04_manufacturing_metadata.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Generated',len(recipes)*2,'linear-data textures')
