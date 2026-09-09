"""Build real regional DSM and imagery without invented detail or edge feather."""
from pathlib import Path
import datetime as dt,hashlib,json,sys,struct
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'work/earth-time/python'))
import numpy as np,requests,tifffile
from PIL import Image
RAW=ROOT/'work/earth-time/terrain-source';OUT=ROOT/'Content/Star/Art/EarthSurface';OUT.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,data):p.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 receipts=json.loads((RAW/'receipts.json').read_text(encoding='utf-8'));sources=[]
 height=np.zeros((7200,7200),np.float32);color=Image.new('RGB',(8192,8192))
 for r in receipts:
  assert r['state']=='downloaded';path=ROOT/r['file'];assert sha(path)==r['sha256']
  with tifffile.TiffFile(path) as f:
   page=f.pages[0];a=page.asarray();scale=page.tags['ModelPixelScaleTag'].value;tie=page.tags['ModelTiepointTag'].value
   assert a.shape==(3600,3600) and abs(scale[0]-1/3600)<1e-12 and tie[3]==r['lon'] and tie[4]==r['lat']+1
   assert np.isfinite(a).all() and a.min()>-1000 and a.max()<9000
  x=(r['lon']-92)*3600;y=(12-r['lat'])*3600;height[y:y+3600,x:x+3600]=a
  sources.append(r)
  name=f"eox2016-{r['lat']}-{r['lon']}.png";p=RAW/name
  params={'SERVICE':'WMS','VERSION':'1.1.1','REQUEST':'GetMap','LAYERS':'s2cloudless','STYLES':'','SRS':'EPSG:4326','BBOX':f"{r['lon']},{r['lat']},{r['lon']+1},{r['lat']+1}",'WIDTH':4096,'HEIGHT':4096,'FORMAT':'image/png'}
  url=requests.Request('GET','https://tiles.maps.eox.at/wms',params=params).prepare().url
  if not p.exists():
   response=requests.get(url,timeout=(15,90));response.raise_for_status();assert response.headers.get('content-type','').startswith('image/');p.write_bytes(response.content)
  im=Image.open(p).convert('RGB');assert im.size==(4096,4096);color.paste(im,((r['lon']-92)*4096,(12-r['lat'])*4096))
  sources.append({'sourceUrl':url,'file':str(p.relative_to(ROOT)),'sha256':sha(p),'date':'2016 mosaic, contains modified Copernicus Sentinel data 2016/2017','license':'CC BY 4.0','providerProcessing':'Provider visual mosaic including ocean/background styling; not raw reflectance or current scenery'})
  print('Loaded',r['lat'],r['lon'],flush=True)
 # Pixel-is-point geographic DSM. Retain all original 30 m samples in the
 # numeric runtime grid; mesh LOD is an independent representation choice.
 binary=ROOT/'Content/Star/Data/andaman-dsm-f32.bin';binary.write_bytes(height.astype('<f4').tobytes())
 # A common display atlas, with no sharpening, noise, inpainting or color key.
 image=OUT/'T_Andaman_Observed_8K.png';color.save(image)
 meta={'schemaVersion':1,'boundsWsen':[92,11,94,13],'width':7200,'height':7200,'pixelIsPoint':True,'stepDegrees':1/3600,'file':binary.name,'sha256':sha(binary),'heightUnits':'meters orthometric height EGM2008','source':'Copernicus DEM GLO-30 public, 2021 release; DSM includes vegetation and structures','sourceResolutionMetersNominal':30,'renderDatum':'Mean-radius game Earth with geographic latitude/longitude parameterization; not a full WGS84 ellipsoid/geoid realization','syntheticHeight':False,'noDataPolicy':'Require all four checked source tiles; no interpolation across absent tiles','minHeightMeters':float(height.min()),'maxHeightMeters':float(height.max()),'imagery':{'file':str(image.relative_to(ROOT)),'sha256':sha(image),'pixels':[8192,8192],'colorSpace':'sRGB provider display mosaic','processing':'Four non-overlapping WMS images packed without resampling, sharpening, smoothing or invented detail'},'sources':sources,'retrievedUtc':dt.datetime.now(dt.timezone.utc).isoformat(),'credit':'Copernicus DEM © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018. Copernicus DEM accessed via AWS Open Data. EOxCloudless by EOX IT Services GmbH, contains modified Copernicus Sentinel data 2016; CC BY 4.0.'}
 dump(ROOT/'Content/Star/Data/andaman-dsm.json',meta);dump(ROOT/'Data/andaman_surface.json',meta)
 print('Built original-resolution DSM',binary.stat().st_size,'and imagery',image.stat().st_size,flush=True)
if __name__=='__main__':main()
