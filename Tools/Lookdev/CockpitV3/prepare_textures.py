"""Small source-bound photo patch and explicitly synthetic manufacturing microfinish."""
from pathlib import Path
from PIL import Image
import numpy as np
import json, hashlib
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'Content/Star/Art/CockpitV3/Textures'
ART=ROOT/'Art/Explorer/CockpitV3'
im=Image.open(ART/'References/orion-interior.jpg')
crop=(3020,1980,3100,2180)
im.crop(crop).save(OUT/'OrionInsulation_BaseColor.png')
n=1024;yy,xx=np.mgrid[:n,:n];rng=np.random.default_rng(30907)
grain=rng.normal(0,1,(n,n))
for key,mean,amplitude in [('Coat',.58,.014),('Alloy',.29,.018),('Graphite',.36,.012),('Cloth',.90,.015)]:
 a=np.clip(mean+grain*amplitude,0,1)
 Image.fromarray((a*255).astype('uint8')).save(OUT/f'{key}_Roughness.png')
# 1024px represents a 10cm swatch; 0.8mm weave repeat, no fabricated stains/damage.
u=xx*2*np.pi/8;v=yy*2*np.pi/8
nx=.14*np.sin(u)*(.7+.3*np.cos(v));ny=.14*np.sin(v)*(.7+.3*np.cos(u))
nz=np.sqrt(1-nx*nx-ny*ny)
Image.fromarray((np.stack((nx*.5+.5,ny*.5+.5,nz*.5+.5),axis=2)*255).astype('uint8')).save(OUT/'Cloth_Normal.png')
source={'retrieved_utc':'2026-09-06T16:20:00Z','sources':[{'id':'jsc2022e044970','image_url':'https://images-assets.nasa.gov/image/jsc2022e044970/jsc2022e044970~orig.jpg','catalog_url':'https://images.nasa.gov/details/jsc2022e044970','credit':'NASA','observation_date':'2016-05-11','description':'Orion Medium Fidelity Mockup at Johnson Space Center. Not ASTER 24. Construction reference plus one insulation pixel patch.','sha256':hashlib.sha256((ART/'References/orion-interior.jpg').read_bytes()).hexdigest(),'texture_crop_px':crop,'texture_dimensions_px':[80,200],'color_space':'sRGB','mapping':'insulation cushion only; camera lighting remains in pixels; no claim of measured albedo','license_basis':'NASA media usage guidelines; no visible people or NASA identifiers included in runtime texture','policy_url':'https://www.nasa.gov/nasa-brand-center/images-and-media/'},{'id':'jsc2022e044162','image_url':'https://images-assets.nasa.gov/image/jsc2022e044162/jsc2022e044162~orig.jpg','credit':'NASA','observation_date':'2014-10-24','use':'reference only, no people/logos in runtime materials'}],'synthetic':['all geometry is original ASTER 24 design inference','roughness maps are calibrated artistic microfinish, not NASA measurements','cloth normal is original synthetic 0.8mm weave','only small insulation patch contains NASA pixels'],'not_claimed':['replica of Orion','photoreal pass','game pass','measured surface properties']}
(ROOT/'Data/cockpit_v3_sources.json').write_text(json.dumps(source,indent=2),encoding='utf-8')
