# Run from repository root; intermediate output belongs to work/lunar-panorama.
import sys,json
from pathlib import Path
sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import cv2,numpy as np
cams=json.loads(Path('work/lunar-panorama/full_cameras.json').read_text())
w,h=6144,1536
az=np.linspace(-np.pi,np.pi,w,endpoint=False);el=np.linspace(np.pi/4,-np.pi/4,h)
A,E=np.meshgrid(az,el);rays=np.stack([np.cos(E)*np.sin(A),-np.sin(E),np.cos(E)*np.cos(A)],axis=-1).astype(np.float32)
sumc=np.zeros((h,w,3),np.float32);sumw=np.zeros((h,w),np.float32)
for c in cams:
 im=cv2.imread(f"Content/Star/Art/LunarPanorama/Source/AS17-147-{c['frame']}HR.jpg");im=cv2.resize(im,(1170,1175));K=np.array(c['K']);R=np.array(c['R']);cam=rays@R;uv=cam@K.T;u=(uv[:,:,0]/uv[:,:,2]).astype(np.float32);v=(uv[:,:,1]/uv[:,:,2]).astype(np.float32)
 mask=(cam[:,:,2]>0)&(u>10)&(u<1160)&(v>10)&(v<1165)
 weight=np.maximum(0,np.minimum(np.minimum(u-10,1160-u),np.minimum(v-10,1165-v)))*mask
 col=cv2.remap(im,u,v,cv2.INTER_LINEAR);sumc+=col*weight[:,:,None];sumw+=weight
out=(sumc/np.maximum(sumw[:,:,None],1e-6)).astype(np.uint8);cv2.imwrite('Content/Star/Art/LunarPanorama/Review/full_sequence_stitch.jpg',out)
