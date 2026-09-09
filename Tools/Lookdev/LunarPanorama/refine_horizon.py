# Run from repository root; intermediate output belongs to work/lunar-panorama.
import sys,json
from pathlib import Path
sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import cv2,numpy as np
from scipy.ndimage import median_filter
from scipy.spatial.transform import Rotation
from scipy.optimize import least_squares
reg=json.loads(Path('work/lunar-panorama/registered.json').read_text());h=np.load('work/lunar-panorama/dem_horizon.npz');az=h['az'];hor=h['elevation'];out=[]
for c in reg['cameras']:
 if c['frame'] not in [22498,22500,22502]:continue
 im=cv2.imread(f"Content/Star/Art/LunarPanorama/Source/AS17-147-{c['frame']}HR.jpg");im=cv2.resize(im,(1170,1175));g=median_filter(cv2.cvtColor(im,cv2.COLOR_BGR2GRAY),size=5);px=[]
 for x in range(36,1135,6):
  yy=np.flatnonzero(g[:800,x]>30)
  if len(yy) and 25<yy[0]<760:px.append([x,int(yy[0]),1])
 px=np.array(px);R0=np.array(c['cameraToEastNorthUp']);K0=np.array(c['K']);hold=np.arange(len(px))%5==0
 def calc(p):
  K=K0.copy();K[0,0]*=p[3];K[1,1]*=p[3];R=Rotation.from_euler('xyz',p[:3],degrees=True).as_matrix()@R0
  vv=(px@np.linalg.inv(K).T)@R.T;vv/=np.linalg.norm(vv,axis=1)[:,None];a=np.rad2deg(np.arctan2(vv[:,0],vv[:,1]))%360;e=np.rad2deg(np.arcsin(vv[:,2]));return e-np.interp(a,az,hor,period=360),K,R,a
 fit=least_squares(lambda p:calc(p)[0][~hold],[0,0,0,1],bounds=([-5,-5,-5,.85],[5,5,5,1.15]),loss='soft_l1',f_scale=.1,max_nfev=150)
 res,K,R,a=calc(fit.x);o=dict(frame=c['frame'],K=K.tolist(),cameraToEastNorthUp=R.tolist(),workSize=[1170,1175],refinement=fit.x.tolist(),trainRmsDeg=float(np.sqrt(np.mean(res[~hold]**2))),holdoutRmsDeg=float(np.sqrt(np.mean(res[hold]**2))),maxAbsoluteDegrees=float(max(abs(res))),skylinePixels=px[:,:2].tolist(),skylineResidualDeg=res.tolist(),holdoutIndices=np.flatnonzero(hold).tolist(),azimuthRange=[float(min((a+180)%360-180)),float(max((a+180)%360-180))]);out.append(o);print(c['frame'],fit.x,o['trainRmsDeg'],o['holdoutRmsDeg'],o['azimuthRange'],flush=True)
reg['refinedCameras']=out;Path('work/lunar-panorama/registered.json').write_text(json.dumps(reg,indent=2),encoding='utf-8')
