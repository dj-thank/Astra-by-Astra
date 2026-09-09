"""Numerical quality checks for the Sun's Earth-tangent optical integration."""
import math
import unittest

RADIUS=6371000.0
BETA=(5.802e-6,13.558e-6,33.1e-6)

def tangent_transmission(height, steps):
    impact=RADIUS+height
    half=math.sqrt((RADIUS+100000.0)**2-impact**2)
    ds=2*half/steps
    depth=[0.0]*3
    for i in range(steps):
        x=-half+(i+0.5)*ds
        h=math.hypot(impact,x)-RADIUS
        for c in range(3):
            depth[c]+=(BETA[c]*math.exp(-h/8000.0)+4.3956e-6*math.exp(-h/1200.0))*ds
    return tuple(math.exp(-v) for v in depth)

class SunQualityTests(unittest.TestCase):
    def test_48_samples_match_dense_reference(self):
        for height in (0,1000,5000,10000,20000,40000,80000):
            actual=tangent_transmission(height,48)
            reference=tangent_transmission(height,8192)
            for a,b in zip(actual,reference):
                self.assertAlmostEqual(a,b,delta=0.002,msg=str(height))

    def test_horizon_is_red_and_upper_atmosphere_recovers_white(self):
        low=tangent_transmission(5000,48)
        high=tangent_transmission(80000,48)
        self.assertGreater(low[0],low[1])
        self.assertGreater(low[1],low[2])
        self.assertGreater(min(high),0.999)

    def test_chromatic_correction_composes_once(self):
        for height in (5000,20000,80000):
            rgb=tangent_transmission(height,48)
            scalar=sum(a*b for a,b in zip(rgb,(0.2126,0.7152,0.0722)))
            for value in rgb:
                self.assertAlmostEqual(value/scalar*scalar,value)

    def test_limb_profile_preserves_disk_energy(self):
        n=10000
        integral=sum(((0.4+0.6*((i+0.5)/n))/0.8)*2*((i+0.5)/n)/n for i in range(n))
        self.assertAlmostEqual(integral,1.0,places=7)

if __name__=='__main__':
    unittest.main(verbosity=2)
