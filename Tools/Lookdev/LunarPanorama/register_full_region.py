"""Register other headings against full observed LROC terrain; frozen north files remain unchanged."""
from pathlib import Path
import sys,json
sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import numpy as np,cv2
from scipy.ndimage import median_filter
from scipy.spatial.transform import Rotation
from scipy.optimize import least_squares
reg=json.loads(Path('Data/lunar_panorama_registration.json').read_text());full=json.loads(Path('Data/lunar_panorama_full_cameras.json').read_text());ref=next(c for c in reg['refinedCameras'] if c['frame']==22500);fc=next(c for c in full if c['frame']==22500);anchor=np.array(ref['cameraToEastNorthUp'])@np.array(fc['R']).T
h=np.load('Content/Star/Art/LunarPanorama/FullRegion/dem_horizon_full.npz');az=h['az'];hor=h['elevation']
configs=[dict(frame=22500,direction='north',columns=[36,1135],threshold=30,companion=22498),dict(frame=22518,direction='southwest',columns=[36,1135],threshold=30,companion=22519),dict(frame=22513,direction='southeast',columns=[36,490],threshold=12,companion=22511),dict(frame=22504,direction='northeast',columns=[36,650],threshold=25,companion=22502),dict(frame=22495,direction='west',columns=[750,1050],threshold=30,companion=22496)]
def pixels(frame,lo,hi,threshold):
 im=cv2.imread(f'Content/Star/Art/LunarPanorama/Source/AS17-147-{frame}HR.jpg');im=cv2.resize(im,(1170,1175));g=median_filter(cv2.cvtColor(im,cv2.COLOR_BGR2GRAY),size=5);out=[]
 for x in range(lo,hi,6):
  if frame==22511:
   # Digitized from the independently inspected held-out image, not from the DEM.
   seeds=np.array([[0,360],[200,386],[400,422],[600,452],[800,505],[960,568],[1100,534],[1250,484],[1400,397],[1600,326],[1800,270],[1900,242],[2000,252],[2150,260],[2340,267]])/2
   center=int(np.interp(x,seeds[:,0],seeds[:,1]));ys=np.arange(max(4,center-28),min(790,center+29));edge=g[ys+3,x].astype(float)-g[ys-3,x].astype(float)
   if max(edge)>8:out.append([x,int(ys[np.argmax(edge)]),1])
   continue
  yy=np.flatnonzero(g[:800,x]>threshold)
  if len(yy) and 25<yy[0]<760:out.append([x,int(yy[0]),1])
 return np.array(out)
def residual(px,K,R):
 vv=px@np.linalg.inv(K).T@R.T;vv/=np.linalg.norm(vv,axis=1)[:,None];a=np.rad2deg(np.arctan2(vv[:,0],vv[:,1]))%360;e=np.rad2deg(np.arcsin(vv[:,2]));return e-np.interp(a,az,hor,period=360),a,e
out=[]
for conf in configs:
 c=next(c for c in full if c['frame']==conf['frame']);R0=anchor@np.array(c['R']);K0=np.array(c['K']);px=pixels(c['frame'],*conf['columns'],conf['threshold']);hold=np.arange(len(px))%5==0
 def calc(p):
  K=K0.copy();K[0,0]*=p[3];K[1,1]*=p[3];R=Rotation.from_euler('xyz',p[:3],degrees=True).as_matrix()@R0
  return residual(px,K,R),K,R
 fit=least_squares(lambda p:np.r_[calc(p)[0][0][~hold],(p[3]-1)*30] if conf['direction']=='southeast' else calc(p)[0][0][~hold],[0,0,0,1],bounds=([-12,-12,-12,.8],[12,12,12,1.2]),loss='soft_l1',f_scale=.12,max_nfev=300)
 (res,a,e),K,R=calc(fit.x);bound=bool(abs(fit.x[3]-.8)<.001 or abs(fit.x[3]-1.2)<.001)
 cc=next(c for c in full if c['frame']==conf['companion']);CR=R@np.array(c['R']).T@np.array(cc['R']);CK=np.array(cc['K']);CK[0,0]*=fit.x[3];CK[1,1]*=fit.x[3]
 # Held-out photograph receives only relative rotation from full sequence, never an independent fit.
 cp=pixels(cc['frame'],36,1135,conf['threshold']);cres,ca,ce=residual(cp,CK,CR)
 center=np.degrees(np.arctan2(R[0,2],R[1,2]));ua=(a-center+180)%360-180+center;uca=(ca-center+180)%360-180+center;amin,amax=np.min(ua),np.max(ua);inside=(uca>=amin)&(uca<=amax);cp=cp[inside];cres=cres[inside];ca=ca[inside];ce=ce[inside]
 wrong=residual(px,K,Rotation.from_euler('z',-10,degrees=True).as_matrix()@R)[0]
 o={**conf,'K':K.tolist(),'cameraToEastNorthUp':R.tolist(),'workSize':[1170,1175],'refinement':fit.x.tolist(),'focalAtBound':bound,'skylinePixels':px[:,:2].tolist(),'skylineResidualDegrees':res.tolist(),'skylineAzimuthDegrees':a.tolist(),'skylineElevationDegrees':e.tolist(),'holdoutIndices':np.flatnonzero(hold).tolist(),'holdoutRmsDegrees':float(np.sqrt(np.mean(res[hold]**2))),'trainRmsDegrees':float(np.sqrt(np.mean(res[~hold]**2))),'wrongHeading10DegreesRms':float(np.sqrt(np.mean(wrong**2))),'independentPhotoHoldout':dict(frame=cc['frame'],samples=len(cp),rmsDegrees=float(np.sqrt(np.mean(cres**2))) if len(cp) else None,K=CK.tolist(),cameraToEastNorthUp=CR.tolist(),pixels=cp[:,:2].tolist(),residualDegrees=cres.tolist(),azimuthDegrees=ca.tolist(),elevationDegrees=ce.tolist()),'azimuthRange':[float(amin),float(amax)]}
 out.append(o);print(c['frame'],conf['direction'],fit.x,'train',o['trainRmsDegrees'],'hold',o['holdoutRmsDegrees'],'companion',o['independentPhotoHoldout']['rmsDegrees'],'wrong',o['wrongHeading10DegreesRms'],'range',o['azimuthRange'],flush=True)
Path('Data/lunar_panorama_full_region_registration.json').write_text(json.dumps(dict(schemaVersion=1,status='BOUNDED_FULL_DTM_REGISTRATION_CANDIDATE',legacyNorthInputsUnchanged=True,globalInitializationAnchorFrozen=True,fullDtmNorthRefinementRecorded=True,sourceCamera=reg['cameraOrigin'],cameras=out,limits=['Relative camera transfer is not a surveyed baseline.','Western West Family summit not covered by full DTM; only Old Family sector fitted.']),indent=2)+'\n',encoding='utf-8',newline='\n')
