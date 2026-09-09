from pathlib import Path
import sys;sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import json,numpy as np,tifffile,hashlib
from scipy.ndimage import map_coordinates
src=Path('./work/trees/data/work/raw/NAC_DTM_APOLLO17.TIF')
with tifffile.TiffFile(src) as t:geo=t.geotiff_metadata
H=tifffile.memmap(src);print('shape',H.shape,'geo',geo,flush=True)
rad=1737400.;lat0,lon0=np.deg2rad([20.1908,30.7717]);sp=geo['ModelPixelScale'][0];left,top=geo['ModelTiepoint'][3:5];x0=rad*np.cos(np.deg2rad(20))*(lon0-np.pi);y0=rad*lat0;c0=(x0-left)/sp-.5;r0=(top-y0)/sp-.5;h0=float(map_coordinates(H,[[r0],[c0]],order=1)[0]);az=np.arange(0,360,.1);ranges=np.arange(500,30000,25.)
A,D=np.meshgrid(np.deg2rad(az),ranges,indexing='ij');lat=lat0+D*np.cos(A)/rad;lon=lon0+D*np.sin(A)/(rad*np.cos(lat0));cc=(rad*np.cos(np.deg2rad(20))*(lon-np.pi)-left)/sp-.5;rr=(top-rad*lat)/sp-.5
z=map_coordinates(H,[rr,cc],order=1,mode='constant',cval=np.nan).astype(np.float64);z[z < -1e30]=np.nan
r=rad+z;dl=lon-lon0;east=r*np.cos(lat)*np.sin(dl);north=r*(np.sin(lat)*np.cos(lat0)-np.cos(lat)*np.sin(lat0)*np.cos(dl));up=r*(np.sin(lat)*np.sin(lat0)+np.cos(lat)*np.cos(lat0)*np.cos(dl))-(rad+h0);el=np.rad2deg(np.arctan2(up-1.6,np.hypot(east,north)));imax=np.nanargmax(el,axis=1);horizon=el[np.arange(len(az)),imax]
np.savez('Content/Star/Art/LunarPanorama/FullRegion/dem_horizon_full.npz',az=az,elevation=horizon,distance=ranges[imax],farthestValidRange=np.max(np.where(np.isfinite(z),D,0),axis=1))
np.savez_compressed('work/lunar-panorama/full-region/visibility.npz',prefix=np.maximum.accumulate(np.nan_to_num(el,nan=-90),axis=1).astype(np.float32),ranges=ranges)
meta=dict(sourcePath=str(src),sourceSha256=hashlib.sha256(src.read_bytes()).hexdigest(),shape=list(H.shape),pixelSpacingMeters=sp,topLeftPixelCornerProjectedMeters=[left,top],cameraGroundHeight=h0,cameraSourcePixel=[c0,r0]);Path('work/lunar-panorama/full-region/dtm.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
for a in range(0,360,15):print(a,round(horizon[a*10],2),ranges[imax[a*10]],flush=True)
