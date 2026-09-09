"""Portable reference samplers. Simulation/render frame conversion happens in runtime."""
import math
import json
from pathlib import Path
import numpy as np

def wrap(value,size):return value%size

def sample_global(array,longitude_degrees,latitude_degrees,scale=0.5):
    if not math.isfinite(longitude_degrees) or not math.isfinite(latitude_degrees):raise ValueError('nonfinite coordinate')
    h,w=array.shape
    x=((longitude_degrees+180)%360)/360*w-0.5
    y=max(0,min(h-1,(90-max(-90,min(90,latitude_degrees)))/180*h-0.5))
    x0=math.floor(x);y0=math.floor(y);fx=x-x0;fy=y-y0
    a=float(array[y0,x0%w])*(1-fx)+float(array[y0,(x0+1)%w])*fx
    b=float(array[min(y0+1,h-1),x0%w])*(1-fx)+float(array[min(y0+1,h-1),(x0+1)%w])*fx
    return (a*(1-fy)+b*fy)*scale

def apollo_pixel(meta,longitude_degrees,latitude_degrees):
    left,top=meta['topLeftPixelCornerProjectedMeters'];r=meta['referenceRadiusMeters'];s=meta['pixelSpacingMeters']
    return ((r*math.cos(math.radians(meta['standardParallelDegrees']))*math.radians(longitude_degrees-meta['centralMeridianDegrees'])-left)/s-0.5,
            (top-r*math.radians(latitude_degrees))/s-0.5)

def sample_apollo(array,mask,meta,longitude_degrees,latitude_degrees):
    x,y=apollo_pixel(meta,longitude_degrees,latitude_degrees)
    h,w=array.shape
    if not (0<=x<w-1 and 0<=y<h-1):return None
    x0=math.floor(x);y0=math.floor(y);fx=x-x0;fy=y-y0
    if not mask[y0:y0+2,x0:x0+2].all():return None
    a=array[y0,x0]*(1-fx)+array[y0,x0+1]*fx;b=array[y0+1,x0]*(1-fx)+array[y0+1,x0+1]*fx
    return float(a*(1-fy)+b*fy)

def sky_uv(ecliptic_direction):
    v=np.asarray(ecliptic_direction,dtype=float);v/=np.linalg.norm(v)
    eps=math.radians(23.439291111111)
    x,y,z=v[0],v[1]*math.cos(eps)-v[2]*math.sin(eps),v[1]*math.sin(eps)+v[2]*math.cos(eps)
    return ((0.5-math.atan2(y,x)/(2*math.pi))%1,0.5-math.asin(max(-1,min(1,z)))/math.pi)

if __name__=='__main__':
    root=Path(__file__).resolve().parents[2]
    meta=json.loads((root/'Content/Star/Data/moon_ldem_16.json').read_text(encoding='utf-8'))
    height=np.fromfile(root/meta['binaryPath'],dtype='<i2').reshape(meta['height'],meta['width'])
    print('Apollo17 global heightMeters=',sample_global(height,30.7717,20.1908))
