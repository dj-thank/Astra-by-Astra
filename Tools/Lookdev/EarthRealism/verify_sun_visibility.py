"""Check a late sunrise image at the independently projected physical Sun."""
from pathlib import Path
import argparse,json,math
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--output',type=Path,required=True);a=p.parse_args()
m=json.loads((a.capture/'scene-0.json').read_text());b=json.loads((a.root/'Content/Star/Data/bodies.json').read_text())['bodies'];s=next(x for x in b if x['id']=='sun')
direction=np.array(s['positionMeters'])-np.array(m['cameraAbsoluteMeters']);distance=np.linalg.norm(direction);direction/=distance
f=np.array(m['cameraForwardSimulation']);up=np.array(m['cameraUpSimulation']);right=np.cross(f,up)
tan=math.tan(math.radians(m['horizontalFovDegrees']/2));depth=float(np.dot(direction,f));assert depth>0
width,height=m['actualViewportWidth'],m['actualViewportHeight']
x=(.5+.5*np.dot(direction,right)/depth/tan)*width
y=(.5-.5*np.dot(direction,up)/depth/tan*width/height)*height
radius=math.asin(s['radiusMeters']/distance)/tan*width/2
image=np.array(Image.open(a.capture/'scene-0.png').convert('RGB')).astype(float)/255
linear=np.where(image<=.04045,image/12.92,((image+.055)/1.055)**2.4)
luma=linear@np.array([.2126,.7152,.0722]);yy,xx=np.ogrid[:height,:width];r=np.sqrt((xx-x)**2+(yy-y)**2)
disk=luma[r<max(2,radius*1.1)];background=luma[(r>radius*3)&(r<radius*6)]
assert len(disk)>4 and len(background)>4
peak=float(disk.max());bg=float(np.median(background));contrast=peak/max(bg,.001)
result={'sunPixelXY':[float(x),float(y)],'solarRadiusPixels':radius,'peakLinearLuminance':peak,'nearbyMedianLuminance':bg,'contrast':contrast,'clearanceDegrees':m['sunHorizonClearanceDegrees'],'scope':'Projected-Sun image visibility, separate from geometric trajectory and physical irradiance calibration'}
assert m['sunHorizonClearanceDegrees']>.25
assert peak>.4 and contrast>2.0,('Solar disk is not visibly distinct at its predicted location',result)
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
