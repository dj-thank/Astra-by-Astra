# Run from repository root; intermediate output belongs to work/lunar-panorama.
import sys,json
from pathlib import Path
sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import cv2,numpy as np
from scipy.ndimage import map_coordinates,median_filter
from scipy.spatial.transform import Rotation
from scipy.optimize import least_squares
root=Path('.');meta=json.loads((root/'Content/Star/Data/apollo17.json').read_text());H=np.fromfile(root/meta['binaryPath'],dtype='<f4').reshape(meta['height'],meta['width'])
rad=1737400.;lat0,lon0=20.1908,30.7717
x0=rad*np.cos(np.deg2rad(20))*np.deg2rad(lon0-180);y0=rad*np.deg2rad(lat0);left,top=meta['topLeftPixelCornerProjectedMeters'];sp=meta['pixelSpacingMeters']
c0=(x0-left)/sp-.5;r0=(top-y0)/sp-.5;z0=float(map_coordinates(H,[[r0],[c0]],order=1)[0])+1.6
az=np.arange(0,360,.1);ranges=np.arange(500,9500,10.)
A,D=np.meshgrid(np.deg2rad(az),ranges,indexing='ij');east=D*np.sin(A);north=D*np.cos(A);cc=c0+east/sp*np.cos(np.deg2rad(20))/np.cos(np.deg2rad(lat0));rr=r0-north/sp
z=map_coordinates(H,[rr,cc],order=1,mode='constant',cval=np.nan)
el=np.rad2deg(np.arctan2(z-z0-D*D/(2*rad),D));imax=np.nanargmax(el,axis=1);horizon=el[np.arange(len(az)),imax]
np.savez('work/lunar-panorama/dem_horizon.npz',az=az,elevation=horizon,distance=ranges[imax])
cs=json.loads(Path('work/lunar-panorama/cameras.json').read_text());observations=[];B=np.array([[1,0,0],[0,0,1],[0,-1,0.]])
for c in cs:
 if c['frame'] not in [22498,22500,22502,22504]:continue
 im=cv2.imread(f"Content/Star/Art/LunarPanorama/Source/AS17-147-{c['frame']}HR.jpg");im=cv2.resize(im,(1170,1175));g=cv2.cvtColor(im,cv2.COLOR_BGR2GRAY);g=median_filter(g,size=5)
 for x in range(30,1140,12):
  if c['frame']==22504 and x>650:continue
  yy=np.flatnonzero(g[:800,x]>30)
  if not len(yy):continue
  y=yy[0]
  if y<25 or y>760:continue
  ray=np.linalg.inv(np.array(c['K']))@np.array([x,y,1]);ray=B@np.array(c['R'])@ray;ray/=np.linalg.norm(ray)
  aa=(np.rad2deg(np.arctan2(ray[0],ray[1]))+330)%360
  if aa<75 or aa>315: observations.append(dict(frame=c['frame'],pixelWork=[x,int(y)],ray=ray.tolist()))
v=np.array([o['ray'] for o in observations])
def rotation(p):return Rotation.from_euler('xyz',[p[1],p[2],-p[0]],degrees=True).as_matrix()
def residual(p):
 vv=v@rotation(p).T;a=np.rad2deg(np.arctan2(vv[:,0],vv[:,1]))%360;e=np.rad2deg(np.arcsin(vv[:,2]));return e-np.interp(a,az,horizon,period=360)
fit=least_squares(residual,[330,0,0],bounds=([300,-10,-10],[345,10,10]),loss='soft_l1',f_scale=.3,max_nfev=100)
res=residual(fit.x);print('fit',fit.x,'RMSE deg',np.sqrt(np.mean(res**2)),'median',np.median(abs(res)),len(res))
for c in cs:c['cameraToEastNorthUp']= (rotation(fit.x)@B@np.array(c['R'])).tolist()
payload=dict(status='APPROXIMATE_PHOTO_TO_DTM_REGISTRATION',cameraOrigin=dict(latitude=lat0,longitude=lon0,heightAboveDtmMeters=1.6,limitation='LM proxy. Schmitt stood NNE of LM; exact offset unmeasured.'),fitParametersYawTiltDegrees=fit.x.tolist(),skylineFit=dict(samples=len(res),rmsDegrees=float(np.sqrt(np.mean(res**2))),medianAbsoluteDegrees=float(np.median(abs(res))),maxAbsoluteDegrees=float(max(abs(res))),fitAzimuthDegrees='315..360 and 0..75',observationThreshold=30),cameras=cs,observations=observations,observationResidualDegrees=res.tolist())
Path('work/lunar-panorama/registered.json').write_text(json.dumps(payload,indent=2),encoding='utf-8')
