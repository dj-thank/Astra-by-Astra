"""Dense CPU scattering reference for focused ground-ray integration."""
from pathlib import Path
import sys,math,numpy as np
ROOT=Path(__file__).resolve().parents[3]
sys.path[:0]=[str(ROOT/'work/earth-v2/deps'),str(ROOT/'Tools/Lookdev/EarthV2')]
import OpenEXR,build_numeric_textures as b
f=OpenEXR.File(str(ROOT/'Content/Star/Art/EarthV2/T_EarthOpticalDepth.exr'),separate_channels=True).channels()
lut=np.stack([f['R'].pixels,f['G'].pixels],axis=-1)
betaR=np.array([5.802e-6,13.558e-6,33.1e-6]);betaM=np.full(3,3.996e-6)
def hit(ro,rd,r):
 t=-np.dot(ro,rd);d=r*r-np.dot(ro+rd*t,ro+rd*t)
 if d<0:return 1e20,-1e20
 q=math.sqrt(d);return t-q,t+q

def integrate(ro,rd,sun,n,biased):
 a,z=hit(ro,rd,1+100000/b.R);a=max(a,0);g,_=hit(ro,rd,1)
 if g>=0:z=min(z,g)
 if z<=a:return np.zeros(3)
 u=np.linspace(0,1,n+1);closest=np.clip(-np.dot(ro,rd),a,z)
 if not biased:t=a+(z-a)*u
 elif closest>=z-1e-10:t=a+(z-a)*(2*u-u*u)
 elif closest<=a+1e-10:t=a+(z-a)*u*u
 else:t=np.where(u<.5,a+(closest-a)*(1-(1-2*u)**2),closest+(z-closest)*(2*u-1)**2)
 p=ro[None,:]+rd[None,:]*((t[:-1]+t[1:])*.5)[:,None]
 ds=np.diff(t)*b.R; h=np.maximum((np.linalg.norm(p,axis=1)-1)*b.R,0)
 dR=np.exp(-h/8000);dM=np.exp(-h/1200)
 depth=(dR[:,None]*betaR+dM[:,None]*betaM*1.1)*ds[:,None]
 prev=np.cumsum(depth,axis=0)-depth
 mu=(p@sun)/np.linalg.norm(p,axis=1)
 columns=b.sample(lut,h,mu)
 sunDepth=columns[:,0,None]*betaR+columns[:,1,None]*betaM*1.1
 shadow=np.array([(lambda interval: interval[0]>1e-6 and interval[1]>=interval[0])(hit(point,sun,1)) for point in p])
 c=np.dot(rd,sun); pr=3*(1+c*c)/(16*np.pi);pm=(1-.76**2)/(4*np.pi*(1+.76**2-2*.76*c)**1.5)
 integ=-np.expm1(-depth)/np.maximum(depth,1e-20)
 source=dR[:,None]*betaR*pr+dM[:,None]*betaM*pm
 value=np.sum(np.exp(-np.minimum(prev+sunDepth,80))*source*ds[:,None]*integ*(~shadow)[:,None],axis=0)
 return value
