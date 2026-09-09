"""Project calibrated NASA mountain pixels onto georeferenced full DTM, in bounded rows."""
from pathlib import Path
import sys,json,time,os,hashlib
sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import numpy as np,cv2,tifffile
from scipy.ndimage import map_coordinates
from PIL import Image
print('PID',os.getpid(),'start',time.time(),flush=True)
reg=json.loads(Path('Data/lunar_panorama_full_region_registration.json').read_text());meta=json.loads(Path('Content/Star/Data/apollo17_far.json').read_text());dtm=json.loads(Path('work/lunar-panorama/full-region/dtm.json').read_text());H=tifffile.memmap(dtm['sourcePath']);left,top=dtm['topLeftPixelCornerProjectedMeters'];rad=1737400.;lat0,lon0=np.deg2rad([20.1908,30.7717]);h0=dtm['cameraGroundHeight'];prefix=np.load('work/lunar-panorama/full-region/visibility.npz')['prefix'];out=Path('Content/Star/Art/LunarPanorama/FullRegion');w=h=8192
farconfidence=np.fromfile(meta['confidencePath'],dtype=np.uint8).reshape(meta['height'],meta['width'])
nearmeta=json.loads(Path('Content/Star/Data/apollo17.json').read_text())
frames=[]
for c in reg['cameras']:
 im=cv2.imread(f"Content/Star/Art/LunarPanorama/Source/AS17-147-{c['frame']}HR.jpg");repair=np.zeros(im.shape[:2],np.uint8);gg=cv2.cvtColor(im,cv2.COLOR_BGR2GRAY).astype(np.float32);dark=cv2.GaussianBlur(gg,(0,0),4)-gg;score=cv2.boxFilter(dark,-1,(85,1))+cv2.boxFilter(dark,-1,(1,85))
 for xx in [272,728,1184,1640,2098]:
  for yy in [264,720,1176,1632,2090]:
   arm=100 if xx==1184 and yy==1176 else 60;dy,dx=np.unravel_index(np.argmax(score[yy-35:yy+36,xx-35:xx+36]),(71,71));cx=xx+dx-35;cy=yy+dy-35;cv2.line(repair,(cx-arm,cy),(cx+arm,cy),255,11);cv2.line(repair,(cx,cy-arm),(cx,cy+arm),255,11)
 frames.append((c,cv2.inpaint(im,repair,7,cv2.INPAINT_TELEA)))
rgba=np.memmap('work/lunar-panorama/full-region/atlas.u8',dtype=np.uint8,mode='w+',shape=(h,w,4));counts={c['direction']:0 for c,_ in frames}
for start in range(0,h,128):
 stop=min(start+128,h);u=(np.arange(w)+.5)/w;v=(np.arange(start,stop)+.5)/h;lon=np.deg2rad(meta['westLongitudeDegrees']+u*(meta['eastLongitudeDegrees']-meta['westLongitudeDegrees']));lat=np.deg2rad(meta['northLatitudeDegrees']-v*(meta['northLatitudeDegrees']-meta['southLatitudeDegrees']));L,P=np.meshgrid(lon,lat);cc=(rad*np.cos(np.deg2rad(20))*(L-np.pi)-left)/5-.5;rr=(top-rad*P)/5-.5;z=map_coordinates(H,[rr,cc],order=1,mode='constant',cval=-1e35).astype(np.float64);valid=z>-1e30;z[~valid]=h0
 r=rad+z;dl=L-lon0;E=r*np.cos(P)*np.sin(dl);N=r*(np.sin(P)*np.cos(lat0)-np.cos(P)*np.sin(lat0)*np.cos(dl));U=r*(np.sin(P)*np.sin(lat0)+np.cos(P)*np.cos(lat0)*np.cos(dl))-(rad+h0)-1.6;vec=np.stack([E,N,U],axis=-1);dist=np.hypot(E,N);az=np.rad2deg(np.arctan2(E,N))%360;el=np.rad2deg(np.arctan2(U,dist));sumc=np.zeros((*E.shape,3),np.float32);sumw=np.zeros(E.shape,np.float32)
 for c,im in frames:
  K=np.array(c['K'])*2;K[2,2]=1;R=np.array(c['cameraToEastNorthUp']);cam=vec@R;uv=cam@K.T;sx=(uv[:,:,0]/np.maximum(uv[:,:,2],.01)).astype(np.float32);sy=(uv[:,:,1]/np.maximum(uv[:,:,2],.01)).astype(np.float32);sk=np.array(c['skylinePixels']);sky=np.interp(sx/2,sk[:,0],sk[:,1])*2;lo,hi=np.array(c['columns'])*2
  if c['frame']==22518:foot=np.interp(sx,[0,600,1200,1800,2340],[825,850,855,865,890])
  elif c['frame']==22513:foot=np.interp(sx,[0,2340],[960,1030])
  elif c['frame']==22504:foot=np.interp(sx,[0,2340],[1370,1380])
  elif c['frame']==22495:foot=np.full(E.shape,795.)
  else:foot=np.interp(sx,[0,800,1600,2340],[1430,1415,1370,1335])
  residual=np.interp(sx/2,sk[:,0],np.abs(c['skylineResidualDegrees']));weight=np.clip(np.minimum(sx-lo,hi-sx)/75,0,1)*np.clip((sy-sky+2)/12,0,1)*np.clip((foot-sy)/35,0,1)*np.clip((.7-residual)/.3,0,1)
  weight*= (cam[:,:,2]>0)&(sy<foot)&(sy>=sky-2)&(sx>lo)&(sx<hi)&(dist>1800)&valid
  # Opaque flank photography is less reliable in the partly shadowed southeast.
  if c['direction']=='southeast':weight*=.65
  col=cv2.remap(im,sx,sy,cv2.INTER_LINEAR);weight*=np.max(col,axis=2)>8;sumc+=col*weight[:,:,None];sumw+=weight;counts[c['direction']]+=int((weight>.01).sum())
 # Reject points hidden by terrain on their view ray. 0.08deg tolerance handles sampling.
 previous=map_coordinates(prefix,[az*10,np.clip((dist-575)/25,0,prefix.shape[1]-1)],order=1,mode='nearest');visible=el>=previous-.08;alpha=np.minimum(sumw,1)*visible*np.clip((dist-1800)/400,0,1)*np.clip((28500-dist)/1500,0,1)
 confidence=map_coordinates(farconfidence,[rr/2,cc/2],order=1,mode='constant',cval=0).astype(np.float32)
 confidence=np.clip((confidence-204)/51,0,1);alpha*=confidence*confidence*(3-2*confidence)
 rgb=(sumc/np.maximum(sumw[:,:,None],1e-7)).clip(0,255).astype(np.uint8);rgba[start:stop,:,:3]=rgb;rgba[start:stop,:,3]=(alpha*255).astype(np.uint8)
 if start%1024==0:print('rows',start,flush=True)
rgba.flush();Image.fromarray(np.asarray(rgba)).save(out/'apollo17_full_region_photo_rgba.png');print('saved',flush=True)
recipe=dict(schemaVersion=1,status='LOCAL_MULTI_DIRECTION_PHOTO_PROJECTION_CANDIDATE',texturePath=(out/'apollo17_full_region_photo_rgba.png').as_posix(),texturePixels=[w,h],geographicBounds={k:meta[k] for k in ['westLongitudeDegrees','eastLongitudeDegrees','northLatitudeDegrees','southLatitudeDegrees']},colorSpace='sRGB interpreted scan RGB; linear alpha',metersPerTexelProjected=[meta['width']*10/w,meta['height']*10/h],sha256=hashlib.sha256((out/'apollo17_full_region_photo_rgba.png').read_bytes()).hexdigest(),nonzeroAlphaPixels=int((rgba[:,:,3]>0).sum()),sourceSupportPixelsBeforeVisibility=counts,sourceFrames=[c['frame'] for c,_ in frames],projection='Fixed measured DTM surfaces; no sky shell, no camera-facing geometry.',maskReasons=['near ground<1.8km','source image outside observed mountain footline','sky/black unusable pixels','unregistered heading including West Family summit','Southeast shadowed/unreliable skyline beyond fitted columns','DTM missing data','terrain hidden by nearer samples','per-column skyline residual>=0.7deg'],north='New full-DTM verification/refinement, legacy files unchanged; root should prefer this verified registration to old crop-only fit.',runtime='Requires apollo17_far10m tier outside old5m region. Original5m interior preserved; corrected300m edge blends tofar. Treat as baked-light photo candidate, not albedo.')
Path('Data/lunar_panorama_full_region_recipe.json').write_text(json.dumps(recipe,indent=2)+'\n',encoding='utf-8',newline='\n');print(json.dumps(recipe),flush=True)
