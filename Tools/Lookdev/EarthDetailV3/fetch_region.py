"""Fetch archived NASA GIBS photographic region; no generated detail or cloud segmentation."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib, json, math, os
import requests
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt
ROOT=Path(__file__).resolve().parents[3]
CACHE=ROOT/'work/earth_detail_v3'
OUT=ROOT/'Content/Star/Art/EarthDetailV3'
DATE='2025-09-06'
LAYER='MODIS_Aqua_CorrectedReflectance_TrueColor'
BASE='https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi'
def sha(b): return hashlib.sha256(b).hexdigest()
def tile(index):
 x,y=index; west=88+4*x; north=19-4*y
 p=dict(SERVICE='WMS',VERSION='1.1.1',REQUEST='GetMap',LAYERS=LAYER,STYLES='',SRS='EPSG:4326',BBOX=f'{west},{north-4},{west+4},{north}',WIDTH=2048,HEIGHT=2048,FORMAT='image/png',TRANSPARENT='TRUE',TIME=DATE)
 url=requests.Request('GET',BASE,params=p).prepare().url
 path=CACHE/f'aqua_{DATE}_{x}_{y}.png'
 if not path.exists():
  r=requests.get(url,timeout=90);r.raise_for_status(); im=Image.open(__import__('io').BytesIO(r.content));assert im.size==(2048,2048);path.write_bytes(r.content)
 b=path.read_bytes();im=Image.open(path).convert('RGB'); assert im.size==(2048,2048)
 print(f'tile {x},{y}: {len(b)} bytes',flush=True)
 return x,y,im,dict(url=url,sha256=sha(b),bytes=len(b),bbox=[west,north-4,west+4,north])
def main():
 CACHE.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
 (CACHE/'process.json').write_text(json.dumps(dict(pid=os.getpid(),created_utc=datetime.now(timezone.utc).isoformat(),owner='astra_earth_detail_v03',task='NASA public imagery fetch and CPU assembly',cwd=str(ROOT),deadline_utc='2026-09-06T16:35:00Z',stop='normal exit; exact PID only')),encoding='utf-8')
 with ThreadPoolExecutor(max_workers=2) as pool: tiles=list(pool.map(tile,[(x,y) for y in range(4) for x in range(4)]))
 canvas=Image.new('RGB',(8192,8192));sources=[]
 for x,y,im,receipt in tiles: canvas.paste(im,(x*2048,y*2048));sources.append(receipt)
 arr=np.asarray(canvas)
 # GIBS true-color JPEG-backed WMS encodes swath absence as exact RGB zero,
 # despite opaque alpha. This is ONLY service-gap exclusion, never a cloud mask.
 valid=np.any(arr!=0,axis=2)
 # Distance calculated on half-size mask to reduce memory; conservative erosion
 # keeps bilinear/anisotropic filtering away from black service-gap boundaries.
 small=Image.fromarray(valid.astype('uint8')*255).resize((2048,2048),Image.Resampling.BOX)
 dist=distance_transform_edt(np.asarray(small)>254)
 alpha=np.clip((dist-3)/16,0,1)
 alpha=Image.fromarray((alpha*255).astype('uint8')).resize((8192,8192),Image.Resampling.BILINEAR)
 canvas.putalpha(alpha)
 dest=OUT/'T_EarthDetail_Andaman_Aqua_20250906_8K.png';canvas.save(dest,compress_level=6)
 preview=canvas.copy();preview.thumbnail((1536,1536));preview.save(CACHE/'region_preview.png')
 center=canvas.crop((3072,3072,5120,5120));center.save(CACHE/'center_1to1.png')
 meta=dict(schema='star.earth-detail.v3',layer=LAYER,observation_date=DATE,retrieved_utc=datetime.now(timezone.utc).isoformat(),product='Daily true-color corrected-reflectance visualization, bands 1-4-3',source_page='https://gis.earthdata.nasa.gov/portal/home/item.html?id=7e2b03d9331b4df785f7c64959df8fd8',product_documentation='https://github.com/nasa-gibs/worldview-options-eosdis/blob/master/common/config/metadata/modis/CorrectedReflectance.md',rights='NASA Earth science data freely available; credit NASA/GSFC, MODIS Aqua, LANCE and GIBS',rights_url='https://www.earthdata.nasa.gov/engage/open-data-services-software-policies/data-use-guidance',texture=str(dest.relative_to(ROOT)).replace('\\','/'),sha256=sha(dest.read_bytes()),bytes=dest.stat().st_size,width=8192,height=8192,bounds=dict(west=88,south=3,east=104,north=19),crs='EPSG:4326',datum='WGS84 geographic; shader uses geographic coordinates on reference sphere',units='degrees east/north; RGB display visualization, not calibrated reflectance',color_space='sRGB interpreted display RGB; imported sRGB=true',pixel_registration='pixel areas, north-up; first center lon88+16/16384 lat19-16/16384',grid_m_per_pixel_equator=2*math.pi*6378137/360*16/8192,source_resolution_m=dict(red=250,green=500,blue=500,gibs_finest_grid=250),nodata_fraction=float(1-valid.mean()),alpha='Exact RGB zero swath-gap exclusion, 12 native-pixel conservative exclusion then64px feather; NOT measured cloud mask or opacity',sources=sources,limitations=['Daily composite with baked cloud shading, atmospheric effects and ocean glint; not intrinsic surface albedo.','No cloud-height/opacity retrieval; clouds stay in registered sphere texture, no volumetric parallax.','Original RGB zero service gaps use underlying global map. No cross-date fill, no generated detail.','Global cloud/weather shading must be replaced, not painted over this regional composite.','4K GPU quality, mip residency and performance require root packaged-game verification.'])
 (ROOT/'Data/earth_detail_v3.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
 print('COMPLETE '+str(dest),flush=True)
if __name__=='__main__':main()

