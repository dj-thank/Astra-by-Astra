"""Build deterministic Earth optical-depth and cloud-density numeric textures."""
from pathlib import Path
import sys, json, math, hashlib, os
from datetime import datetime, timezone, timedelta
import numpy as np
from scipy.spatial import cKDTree
from scipy.special import roots_legendre
from PIL import Image
import psutil
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'work/earth-v2/deps'))
import OpenEXR
OUT=ROOT/'Content/Star/Art/EarthV2'
WORK=ROOT/'work/earth-v2'
R=6371008.4; TOP=100000.; WIDTH=512; HEIGHT=256; MARGIN=0.012

def columns(height,mu,order=128):
    height,mu=np.broadcast_arrays(np.asarray(height,dtype=np.float64),np.asarray(mu,dtype=np.float64))
    radius=R+height
    end=np.maximum(-radius*mu+np.sqrt(np.maximum((R+TOP)**2-radius**2*(1-mu*mu),0)),0)
    nodes,weights=roots_legendre(order)
    distance=end[...,None]*(nodes+1)*0.5
    altitude=np.maximum(np.sqrt(radius[...,None]**2+distance**2+2*radius[...,None]*mu[...,None]*distance)-R,0)
    rayleigh=np.sum(np.exp(-altitude/8000.)*weights,axis=-1)*end*.5
    mie=np.sum(np.exp(-altitude/1200.)*weights,axis=-1)*end*.5
    return np.stack([rayleigh,mie],axis=-1)

def uv(height,mu):
    height=np.clip(height,0,TOP)
    low=-np.sqrt(np.maximum(1-(R/(R+height))**2,0))-MARGIN
    return np.cbrt(np.clip((mu-low)/(1-low),0,1)),np.sqrt(height/TOP)

def sample(lut,height,mu):
    u,v=uv(np.asarray(height),np.asarray(mu)); x=u*(WIDTH-1);y=v*(HEIGHT-1)
    x0=np.floor(x).astype(int);y0=np.floor(y).astype(int);x1=np.minimum(x0+1,WIDTH-1);y1=np.minimum(y0+1,HEIGHT-1)
    fx=(x-x0)[...,None];fy=(y-y0)[...,None]
    return ((1-fy)*((1-fx)*lut[y0,x0]+fx*lut[y0,x1])+fy*((1-fx)*lut[y1,x0]+fx*lut[y1,x1]))*1e6

def main():
    OUT.mkdir(parents=True,exist_ok=True);WORK.mkdir(parents=True,exist_ok=True)
    now=datetime.now(timezone.utc)
    (WORK/'numeric-process.json').write_text(json.dumps({'pid':os.getpid(),'created':datetime.fromtimestamp(psutil.Process().create_time(),timezone.utc).isoformat(),'owner':'earth-v2','purpose':'numeric texture generation','deadline':(now+timedelta(minutes=20)).isoformat(),'stop':'natural exit'}),encoding='utf-8')
    lut=np.empty((HEIGHT,WIDTH,2),dtype=np.float32)
    s=np.linspace(0,1,WIDTH)[None,:]
    for first in range(0,HEIGHT,16):
        h=TOP*(np.arange(first,min(first+16,HEIGHT))/(HEIGHT-1))**2
        low=-np.sqrt(1-(R/(R+h))**2)-MARGIN
        mu=low[:,None]+(1-low[:,None])*s**3
        lut[first:first+len(h)]=columns(h[:,None],mu)/1e6
    # Half precision matches the intended uncompressed GPU FloatRGBA texture.
    lut=lut.astype(np.float16).astype(np.float32)
    channels={'R':np.ascontiguousarray(lut[:,:,0]),'G':np.ascontiguousarray(lut[:,:,1]),'B':np.zeros((HEIGHT,WIDTH),np.float32)}
    OpenEXR.File({'compression':OpenEXR.ZIP_COMPRESSION,'type':OpenEXR.scanlineimage},channels).write(str(OUT/'T_EarthOpticalDepth.exr'))
    stored=OpenEXR.File(str(OUT/'T_EarthOpticalDepth.exr'),separate_channels=True).channels()
    assert np.array_equal(stored['R'].pixels,lut[:,:,0])
    assert np.array_equal(stored['G'].pixels,lut[:,:,1])
    assert np.count_nonzero(stored['B'].pixels)==0
    rng=np.random.default_rng(20260908)
    h=rng.uniform(0,1,1600)**2*TOP
    horizon=-np.sqrt(1-(R/(R+h))**2)
    mu=horizon+(1-horizon)*rng.uniform(0,1,1600)**3
    expected=columns(h,mu,512);actual=sample(lut,h,mu)
    beta=np.array([[5.802e-6,4.3956e-6],[13.558e-6,4.3956e-6],[33.1e-6,4.3956e-6]])
    transmission_error=np.abs(np.exp(-expected@beta.T)-np.exp(-actual@beta.T))
    vertical_h=np.array([0,1200,8000,25000,80000.])
    analytic=np.stack([scale*(np.exp(-vertical_h/scale)-np.exp(-TOP/scale)) for scale in (8000.,1200.)],axis=-1)
    vertical_error=np.max(np.abs(columns(vertical_h,np.ones_like(vertical_h))-analytic)/np.maximum(analytic,1e-8))
    assert vertical_error<1e-6,vertical_error
    assert np.max(transmission_error)<0.015,float(np.max(transmission_error))
    print('LUT max transmission error',float(np.max(transmission_error)),flush=True)
    # Periodic 3-D Worley density. Synthetic cloud shaping, not observed detail.
    size=64;coords=np.stack(np.meshgrid(*[np.arange(size)]*3,indexing='ij'),axis=-1).reshape(-1,3)
    volume=np.zeros(len(coords),np.float64)
    for cells,weight in [(4,.5),(8,.35),(16,.15)]:
        spacing=size/cells
        grid=np.stack(np.meshgrid(*[np.arange(cells)]*3,indexing='ij'),axis=-1).reshape(-1,3)
        points=(grid+rng.uniform(.1,.9,grid.shape))*spacing
        distances=cKDTree(points,boxsize=size).query(coords,k=1,workers=2)[0]
        volume+=weight*(1-np.clip(distances/(spacing*.95),0,1))
    volume=volume.reshape((size,size,size))
    atlas=np.empty((528,528),np.uint8)
    for z in range(size):
        tile=np.pad(np.rint(volume[:,:,z]*255).astype(np.uint8),1,mode='wrap')
        x=z%8;y=z//8;atlas[y*66:(y+1)*66,x*66:(x+1)*66]=tile
        assert np.array_equal(tile[0,1:-1],tile[-2,1:-1])
        assert np.array_equal(tile[1:-1,0],tile[1:-1,-2])
    Image.fromarray(atlas).save(OUT/'T_CloudDensityAtlas.png')
    report={'version':2,'earthRadiusMeters':R,'atmosphereTopMeters':TOP,'rayleighScaleHeightMeters':8000,'mieScaleHeightMeters':1200,'lutDimensions':[WIDTH,HEIGHT],'channelUnits':'R/G: Rayleigh/Mie density path integrals in million metres','linear':True,'EXRRoundTripExact':True,'observedData':False,'quadratureOrder':128,'referenceOrder':512,'referenceRays':1600,'maximumRGBTransmissionAbsoluteError':float(np.max(transmission_error)),'p99RGBTransmissionAbsoluteError':float(np.quantile(transmission_error,.99)),'verticalIntegralRelativeError':float(vertical_error),'noise':{'type':'periodic Worley synthesis','voxelSize':64,'atlasSize':528,'tileSize':66,'seed':20260908,'observedCloudDetail':False},'OpenEXRVersion':OpenEXR.__version__,'outputs':{}}
    for path in (OUT/'T_EarthOpticalDepth.exr',OUT/'T_CloudDensityAtlas.png'):
        report['outputs'][str(path.relative_to(ROOT))]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size}
    (ROOT/'Data/earth_v2_numeric_textures.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('NUMERIC TEXTURES VERIFIED',flush=True)
if __name__=='__main__':main()
