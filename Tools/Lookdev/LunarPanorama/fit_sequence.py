# Run from repository root; intermediate output belongs to work/lunar-panorama.
import sys,json,os,time
from pathlib import Path
sys.path.insert(0,str(Path('work/lunar-panorama/python').resolve()))
import cv2,numpy as np
print('PID',os.getpid(),'started',time.time(),flush=True)
ids=[22493,22494,22495,22496,22497,22498,22500,22502,22504,22505]
ims=[];feats=[];sift=cv2.SIFT_create(nfeatures=7000)
for n in ids:
 im=cv2.imread(f'Content/Star/Art/LunarPanorama/Source/AS17-147-{n}HR.jpg');im=cv2.resize(im,(1170,1175));ims.append(im)
 mask=np.zeros(im.shape[:2],np.uint8);mask[15:850,15:-15]=255
 feats.append(cv2.detail.computeImageFeatures2(sift,im,mask))
matcher=cv2.detail.BestOf2NearestMatcher_create(False,0.3)
pairs=matcher.apply2(feats);matcher.collectGarbage()
for p in pairs:
 if p.src_img_idx<p.dst_img_idx and p.num_inliers>10:print(ids[p.src_img_idx],ids[p.dst_img_idx],p.num_inliers,p.confidence,flush=True)
ok,cams=cv2.detail_HomographyBasedEstimator().apply(feats,pairs,None);print('estimate',ok,flush=True)
for c in cams:c.R=c.R.astype(np.float32)
adj=cv2.detail_BundleAdjusterRay();adj.setConfThresh(0.7);adj.setRefinementMask(np.array([[1,0,1],[0,1,1],[0,0,0]],np.uint8))
ok,cams=adj.apply(feats,pairs,cams);print('adjust',ok,flush=True)
Rs=cv2.detail.waveCorrect([c.R for c in cams],cv2.detail.WAVE_CORRECT_HORIZ)
out=[]
for n,c,r in zip(ids,cams,Rs):
 out.append(dict(frame=n,K=c.K().tolist(),R=r.tolist(),focal=c.focal,workSize=[1170,1175]));print(n,c.focal,flush=True)
Path('work/lunar-panorama/cameras.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
