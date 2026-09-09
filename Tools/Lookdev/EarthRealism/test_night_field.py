"""Check energy and continuity of the explicitly fictional night display field."""
import unittest,numpy as np
Y=np.array([.2126,.7152,.0722])
def smooth(a,b,x):
 t=np.clip((x-a)/(b-a),0,1);return t*t*(3-2*t)
def field(q,footprint):
 q=np.asarray(q,float);cell=np.floor(q);within=q-cell;variance=.0121+footprint*footprint/12
 out=np.zeros(q.shape[:-1]+(3,))
 for iy in [-1,0,1]:
  for ix in [-1,0,1]:
   offset=np.array([ix,iy]);key=cell+offset
   seeds=np.stack([key@np.array([127.1,311.7]),key@np.array([269.5,183.3])],-1)
   jitter=np.mod(np.sin(seeds)*43758.5453,1)
   delta=within-(offset+.25+jitter*.5)
   energy=np.exp(-np.sum(delta*delta,axis=-1)/(2*variance))/(2*np.pi*variance)
   t=jitter[...,0,None];tint=(1-t)*np.array([1,.75,.45])+t*np.array([.72,.88,1])
   tint/=np.maximum(tint@Y,.1)[...,None]
   out+=energy[...,None]*tint
 resolved=1-smooth(.65,1.5,footprint)
 return (1-resolved)+out*resolved
class NightFieldTests(unittest.TestCase):
 def test_mean_luminance_is_not_arbitrary_brightness_gain(self):
  axis=(np.arange(512)+.5)/16;xx,yy=np.meshgrid(axis,axis);q=np.stack([xx,yy],-1)
  mean=float(np.mean(field(q,.3)@Y));self.assertAlmostEqual(mean,1,delta=.015)
 def test_averages_to_original_when_subpixel(self):
  self.assertTrue(np.array_equal(field([[1.2,3.4],[5.6,7.8]],1.6),np.ones((2,3))))
 def test_world_cell_boundaries_are_continuous(self):
  for fp in [.2,.65,1.0,1.4]:
   left=field([[12-1e-6,5.37]],fp);right=field([[12+1e-6,5.37]],fp)
   self.assertLess(float(np.max(abs(left-right))),.004)
 def test_no_light_added_where_source_is_zero(self):
  self.assertTrue(np.array_equal(0*field([[1.2,3.4]],.3),np.zeros((1,3))))
if __name__=='__main__':unittest.main(verbosity=2)
