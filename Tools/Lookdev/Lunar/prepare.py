import json,urllib.request,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[3]
out=root/'Content/Star/Art/Lookdev/Lunar'
cache=root/'work/lunar-source'; cache.mkdir(parents=True,exist_ok=True)
meta=json.loads((cache/'files.json').read_text(encoding='utf-8-sig'))
records=[]
for kind in ['Diffuse','nor_dx','Rough']:
 d=meta[kind]['2k']['png']; p=cache/Path(d['url']).name
 if not p.exists(): urllib.request.urlretrieve(d['url'],p)
 assert hashlib.md5(p.read_bytes()).hexdigest()==d['md5']
 records.append(dict(kind=kind,url=d['url'],sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size))
from PIL import Image
import numpy as np
rgb=np.array(Image.open(cache/'gravelly_sand_diff_2k.png').convert('RGB'),dtype=np.float32)/255
linear=np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
luma=linear@np.array([.2126,.7152,.0722])
# Mean-neutral limited reflectance modulation; no AO, displacement, or preview illumination.
ratio=np.clip(luma/luma.mean(),.5,1.5)
encoded=.5+(ratio-1)*.5
rough_image=Image.open(cache/'gravelly_sand_rough_2k.png')
rough=np.array(rough_image,dtype=np.float32)
rough/=65535.0 if rough_image.mode in ('I','I;16') else 255.0
packed=np.stack([encoded,rough,np.zeros_like(rough)],-1)
Image.fromarray(np.uint8(np.rint(packed*255))).save(out/'lunar_detail_linear.png')
Image.open(cache/'gravelly_sand_nor_dx_2k.png').convert('RGB').save(out/'lunar_detail_normal_dx.png')
manifest=dict(source='https://polyhaven.com/a/gravelly_sand',license='CC0-1.0',license_url='https://polyhaven.com/license',author='Dario Barresi',retrieved='2026-09-06',observation_date='not supplied',source_tile_width_m=2.5,lunar_usage='Artistic terrestrial granular analog; not measured lunar microgeometry',inputs=records,processing='Diffuse sRGB decoded to linear luminance, divided by mean, clamped .5..1.5, encoded R=.5+.5*(ratio-1); G=source roughness. No AO or height applied.',outputs={})
for p in out.glob('*.png'): manifest['outputs'][p.name]=dict(sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size,color_space='linear data',size=[2048,2048])
manifest['outputs']['lunar_detail_normal_dx.png']['normal_convention']='DirectX tangent normal, -Y, import normalmap and no green flip; sample with UE Normal sampler returns decoded signed vector'
(out/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps(manifest,indent=2))

