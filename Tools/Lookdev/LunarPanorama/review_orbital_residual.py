from pathlib import Path
import json,hashlib,sys
sys.path.insert(0,'work/lunar-panorama/python')
import numpy as np,cv2,tifffile
from PIL import Image,ImageDraw,ImageFont
p=Path('Data/lunar_panorama_orbital_residual.json');m=json.loads(p.read_text());out=Path('Content/Star/Art/LunarPanorama/OrbitalResidual');a=np.asarray(Image.open(m['texturePath']));x0,y0,x1,y1=m['eastWindow']['pixelBox'];crop=a[y0:y1,x0:x1];r=(crop[:,:,0].astype(float)-128)/127;confidence=crop[:,:,1].astype(float)/255*crop[:,:,3]/255;factor=1+r*confidence
far=json.loads(Path('Content/Star/Data/apollo17_far.json').read_text());sx=(np.arange(x0,x1,dtype=np.float32)+.5)*(far['width']*2/8192)-1;sy=(np.arange(y0,y1,dtype=np.float32)+.5)*(far['height']*2/8192)-1;X,Y=np.meshgrid(sx,sy);src=tifffile.memmap(m['sourcePath']);raw=cv2.remap(src,X,Y,cv2.INTER_LINEAR)
linear=.18*factor;display=np.where(linear<=.0031308,12.92*linear,1.055*linear**(1/2.4)-.055);prediction=np.repeat((np.clip(display,0,1)*255).astype(np.uint8)[:,:,None],3,axis=2)
neutral=np.full_like(prediction,int((1.055*.18**(1/2.4)-.055)*255));panels=[(Image.fromarray(raw).convert('RGB'),'Observed LROC mosaic / contains original illumination'),(Image.fromarray(neutral),'Neutral surface before residual'),(Image.fromarray(prediction),'Neutral surface x residual / strength1 (not a game render)'),(Image.fromarray((confidence*255).astype(np.uint8)).convert('RGB'),'Confidence x eligibility / white means supported')]
sheet=Image.new('RGB',(1440,1090),(20,23,28));d=ImageDraw.Draw(sheet);font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',19)
for i,(im,label) in enumerate(panels):
 im=im.resize((710,492),Image.Resampling.LANCZOS);xx=(i%2)*720;yy=(i//2)*540+35;sheet.paste(im,(xx,yy));d.text((xx+8,yy-26),label,font=font,fill='white')
sheet.save(out/'east_residual_comparison.jpg',quality=93)
# Check every destination texel within1.8km, rather than only one center value.
b=far;lon0,lat0=30.7717,20.1908;cx=int((lon0-b['westLongitudeDegrees'])/(b['eastLongitudeDegrees']-b['westLongitudeDegrees'])*8192);cy=int((b['northLatitudeDegrees']-lat0)/(b['northLatitudeDegrees']-b['southLatitudeDegrees'])*8192);xs=np.arange(cx-400,cx+401);ys=np.arange(cy-400,cy+401);L,P=np.meshgrid(np.deg2rad(b['westLongitudeDegrees']+(xs+.5)/8192*(b['eastLongitudeDegrees']-b['westLongitudeDegrees'])),np.deg2rad(b['northLatitudeDegrees']-(ys+.5)/8192*(b['northLatitudeDegrees']-b['southLatitudeDegrees'])));la,lo=np.deg2rad([lat0,lon0]);distance=1737400*np.arccos(np.clip(np.sin(la)*np.sin(P)+np.cos(la)*np.cos(P)*np.cos(L-lo),-1,1));sub=a[ys[0]:ys[-1]+1,xs[0]:xs[-1]+1];inner=distance<=1800;assert sub[:,:,3][inner].max()==0
assert np.max(abs((a[:,:,0].astype(np.int16)-128)/127))<=.324
assert hashlib.sha256(Path(m['texturePath']).read_bytes()).hexdigest()==m['sha256']
for key,value in m['geographicBounds'].items():assert value==far[key]
m['validation']=dict(inner1800mPixelsChecked=int(inner.sum()),innerAlphaMax=int(sub[:,:,3][inner].max()),boundsExactlyMatchFarTier=True,sourceOriginalHashStillMatches=hashlib.sha256(Path(m['sourcePath']).read_bytes()).hexdigest()==m['sourceSha256'],fullPhotoAtlasUnchanged=hashlib.sha256(Path('Content/Star/Art/LunarPanorama/FullRegion/apollo17_full_region_photo_rgba.png').read_bytes()).hexdigest()==json.loads(Path('Data/lunar_panorama_full_region_recipe.json').read_text())['sha256']);m['reviewImage']=(out/'east_residual_comparison.jpg').as_posix();p.write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8',newline='\n');print(json.dumps(m['validation']))
