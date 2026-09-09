# Run from repository root; intermediate output belongs to work/lunar-panorama.
import sys,json,hashlib
from pathlib import Path
sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import cv2,numpy as np
from scipy.ndimage import map_coordinates
REG=json.loads(Path('work/lunar-panorama/registered.json').read_text());META=json.loads(Path('Content/Star/Data/apollo17.json').read_text());H=np.fromfile(META['binaryPath'],dtype='<f4').reshape(META['height'],META['width']);rad=META['referenceRadiusMeters'];lat0,lon0=np.deg2rad([20.1908,30.7717]);left,top=META['topLeftPixelCornerProjectedMeters'];sp=META['pixelSpacingMeters'];x0=rad*np.cos(np.deg2rad(20))*(lon0-np.pi);y0=rad*lat0;c0=(x0-left)/sp-.5;r0=(top-y0)/sp-.5;h0=float(map_coordinates(H,[[r0],[c0]],order=1)[0])
def points(cc,rr):
 z=map_coordinates(H,[rr,cc],order=1).astype(np.float64);lat=(top-(rr+.5)*sp)/rad;lon=np.pi+(left+(cc+.5)*sp)/(rad*np.cos(np.deg2rad(20)));dl=lon-lon0;r=rad+z
 return np.stack([r*np.cos(lat)*np.sin(dl),r*(np.sin(lat)*np.cos(lat0)-np.cos(lat)*np.sin(lat0)*np.cos(dl)),r*(np.sin(lat)*np.sin(lat0)+np.cos(lat)*np.cos(lat0)*np.cos(dl))-(rad+h0)],axis=-1)
# Texture bounds are source pixel outer edges, retained in metadata.
cmin,cmax,rmin,rmax=350,1800,80,1000;w,h=3072,2048
cc,rr=np.meshgrid(np.linspace(cmin-.5,cmax-.5,w,endpoint=False)+(cmax-cmin)/w/2,np.linspace(rmin-.5,rmax-.5,h,endpoint=False)+(rmax-rmin)/h/2)
p=points(cc,rr);v=p-np.array([0,0,1.6]);dist=np.linalg.norm(v[:,:,:2],axis=2);aa=np.rad2deg(np.arctan2(v[:,:,0],v[:,:,1]));ee=np.rad2deg(np.arctan2(v[:,:,2],dist));sumc=np.zeros((h,w,3),np.float32);sumw=np.zeros((h,w),np.float32)
for c in REG['refinedCameras']:
 if c['frame'] not in [22498,22500]:continue
 R=np.array(c['cameraToEastNorthUp']);K=np.array(c['K'])*2;K[2,2]=1;cam=v@R;uv=cam@K.T;u=(uv[:,:,0]/uv[:,:,2]).astype(np.float32);vy=(uv[:,:,1]/uv[:,:,2]).astype(np.float32)
 im=cv2.imread(f"Content/Star/Art/LunarPanorama/Source/AS17-147-{c['frame']}HR.jpg")
 # Avoid sky above the digitized silhouette by a 5-work-pixel conservative margin.
 sky=np.array(c['skylinePixels']);sy=np.interp(u/2,sky[:,0],sky[:,1])*2
 foot=np.interp(u,[0,800,1600,2340], [1540,1500,1470,1440] if c['frame']==22498 else [1430,1415,1370,1335])
 mask=(cam[:,:,2]>0)&(u>70)&(u<2270)&(vy>sy+10)&(vy<foot-15)
 weight=np.clip(np.minimum(u-70,2270-u)/250,0,1)*np.clip((foot-vy-15)/70,0,1)*mask
 if c['frame']==22498:weight*=.6
 repair=np.zeros(im.shape[:2],np.uint8)
 gg=cv2.cvtColor(im,cv2.COLOR_BGR2GRAY).astype(np.float32);dark=cv2.GaussianBlur(gg,(0,0),4)-gg;score=cv2.boxFilter(dark,-1,(85,1))+cv2.boxFilter(dark,-1,(1,85))
 for xx in [272,728,1184,1640,2098]:
  for yy in [264,720,1176,1632,2090]:
   arm=100 if xx==1184 and yy==1176 else 60
   dy,dx=np.unravel_index(np.argmax(score[yy-35:yy+36,xx-35:xx+36]),(71,71));cx=xx+dx-35;cy=yy+dy-35
   cv2.line(repair,(cx-arm,cy),(cx+arm,cy),255,11);cv2.line(repair,(cx,cy-arm),(cx,cy+arm),255,11)
 im=cv2.inpaint(im,repair,7,cv2.INPAINT_TELEA)
 col=cv2.remap(im,u,vy,cv2.INTER_LINEAR);sumc+=col*weight[:,:,None];sumw+=weight
rgb=(sumc/np.maximum(sumw[:,:,None],1e-8)).clip(0,255).astype(np.uint8)
# Reject terrain hidden behind any closer DTM sample on same ray.
visible=np.ones((h,w),bool)
for frac in np.linspace(.04,.98,32):
 qq=points(c0+(cc-c0)*frac,r0+(rr-r0)*frac);qd=np.linalg.norm(qq[:,:,:2],axis=2);qe=np.rad2deg(np.arctan2(qq[:,:,2]-1.6,qd));visible &= qe < ee+.08
# Confidence and regional feather: the photo is never a near-ground substitute.
alpha=np.clip((dist-1800)/400,0,1)*np.clip((6500-dist)/500,0,1)*np.clip((aa+48)/5,0,1)*np.clip((4-aa)/4,0,1)*np.clip((ee-2.0)/1.2,0,1)*visible*(sumw>0)
rgba=np.dstack([rgb, (alpha*255).astype(np.uint8)]);out=Path('Content/Star/Art/LunarPanorama');cv2.imwrite(str(out/'north_massif_photo_projection_rgba.png'),rgba)
neutral=(rgb.astype(float)*alpha[:,:,None]+70*(1-alpha[:,:,None])).clip(0,255).astype(np.uint8);cv2.imwrite(str(out/'north_massif_projection_on_neutral.png'),neutral)
# Direct measured DTM vertex samples, no displacement. Coordinates E,N,U in meters.
step=5;cols=np.arange(cmin,cmax,step);rows=np.arange(rmin,rmax,step);C,Rr=np.meshgrid(cols,rows);P=points(C,Rr);uvs=np.stack([(C+.5-cmin)/(cmax-cmin),1-(Rr+.5-rmin)/(rmax-rmin)],axis=-1)
lines=['# Apollo17 North Massif measured LROC DTM. E,N,U meters relative LM DTM ground.','mtllib north_massif.mtl','o NorthMassifMeasuredDTM']
lines += ['v %.6f %.6f %.6f'%tuple(x) for x in P.reshape(-1,3)];lines += ['vt %.8f %.8f'%tuple(x) for x in uvs.reshape(-1,2)];lines+=['usemtl NorthMassifPhotoCandidate'];nw=len(cols)
for y in range(len(rows)-1):
 for x in range(nw-1):
  i=y*nw+x+1;j=i+nw
  # Rows descend south. Reverse winding for positive U normal.
  lines.extend([f'f {i}/{i} {j}/{j} {i+1}/{i+1}',f'f {i+1}/{i+1} {j}/{j} {j+1}/{j+1}'])
(out/'north_massif_measured_enu_m.obj').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n');(out/'north_massif.mtl').write_text('newmtl NorthMassifPhotoCandidate\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nmap_Kd north_massif_photo_projection_rgba.png\n',encoding='utf-8',newline='\n')
info=dict(texture='Content/Star/Art/LunarPanorama/north_massif_photo_projection_rgba.png',nativeTexturePixels=[w,h],sourcePixelOuterBounds=[cmin,rmin,cmax,rmax],geoBounds=dict(west=180+np.rad2deg((left+cmin*sp)/(rad*np.cos(np.deg2rad(20)))),east=180+np.rad2deg((left+cmax*sp)/(rad*np.cos(np.deg2rad(20)))),north=np.rad2deg((top-rmin*sp)/rad),south=np.rad2deg((top-rmax*sp)/rad)),photoFrames=[22498,22500],acceptedAzimuthDegrees=[312,360,0,4],minimumDistanceMeters=1800,maximumDistanceMeters=6500,nonzeroAlphaPixels=int((alpha>0).sum()),alphaCoverageFraction=float((alpha>0).mean()),mesh=dict(vertices=int(P.size/3),triangles=int((len(rows)-1)*(nw-1)*2),vertexSpacingMeters=25,axes='E,N,U meters; UE X=N*100 Y=E*100 Z=U*100',referenceGroundHeightMeters=float(h0)),limit='Approximate image registration, baked 1972 lighting; alpha does not certify backside coverage. No DEM displacement.')
Path('work/lunar-panorama/projection.json').write_text(json.dumps(info,indent=2),encoding='utf-8',newline='\n');print(json.dumps(info),flush=True)
