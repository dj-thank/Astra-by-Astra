"""Optical regression checks; CPU math + existing exact-source HLSL compiler, no GPU.
Run from any directory: python Tools/Lookdev/Orbit/verify_orbit.py --compile-hlsl
"""
from pathlib import Path
import argparse
import json
import math
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'Tools/Unreal/Materials'))
from verify_materials import compile_hlsl, MaterialInvariantTests


def segment_weight(depth):
    x = min(depth, 80.0)
    return 1-x/2+x*x/6 if depth < .001 else (1-math.exp(-x))/max(depth, 1e-8)


def ring_transmitted(tau, mu, mu0):
    a, b = tau/mu, tau/mu0
    x = abs(a-b)
    weight = 1-x/2+x*x/6 if x < .001 else (1-math.exp(-min(x, 80)))/max(x, 1e-8)
    return a*math.exp(-min(a, b, 80))*weight


def smith_g1(mu, alpha):
    return 2*mu/max(mu+math.sqrt(alpha*alpha+(1-alpha*alpha)*mu*mu), 1e-8)


def midpoint_integral(fn, count=100000):
    return sum(fn((i+.5)/count) for i in range(count))/count


class OrbitOpticsTests(unittest.TestCase):
    def test_segment_zero_thin_thick_matches_independent_beer_integral(self):
        for depth in (0, 1e-12, 1e-5, .000999, .001, .01, 1, 8, 50, 100):
            reference = midpoint_integral(lambda t: math.exp(-depth*t))
            self.assertAlmostEqual(segment_weight(depth), reference, delta=2e-8)
        # Regression witness: old midpoint dims an optically thick constant slab.
        self.assertLess(math.exp(-8/2)/segment_weight(8), .15)

    def test_segment_split_invariance_and_energy_bound(self):
        for depth in (0, 1e-8, .1, 2, 8, 80):
            reference = -math.expm1(-depth)
            for count in (1, 4, 16, 24):
                ds = depth/count
                scattered = sum(math.exp(-i*ds)*ds*segment_weight(ds) for i in range(count))
                self.assertAlmostEqual(scattered, reference, delta=2e-10)
                self.assertGreaterEqual(scattered, 0)
                self.assertLessEqual(scattered, 1+1e-12)

    def test_ring_transmitted_matches_depth_quadrature(self):
        for tau in (0, 1e-9, .0001, .1, 1, 5):
            for mu, mu0 in ((.5,.5),(.50000001,.5),(.49999999,.5),(.1,.9),(.9,.1),(.002,.2)):
                # Integrate scattering at fractional depth t measured from view side.
                ref = midpoint_integral(lambda t: tau/mu*math.exp(-tau*t/mu-tau*(1-t)/mu0))
                val = ring_transmitted(tau,mu,mu0)
                self.assertTrue(math.isfinite(val))
                self.assertGreaterEqual(val,0)
                self.assertAlmostEqual(val,ref,delta=1e-7)

    def test_ring_zero_density_equal_angle_and_reciprocity(self):
        for mu in (.002,.1,.5,1):
            self.assertEqual(ring_transmitted(0,mu,.5),0)
            for tau in (1e-8,.01,1,5):
                a=tau/mu
                self.assertAlmostEqual(ring_transmitted(tau,mu,mu),a*math.exp(-min(a,80)))
                for mu0 in (.002,.3,1):
                    self.assertAlmostEqual(mu*ring_transmitted(tau,mu,mu0),
                                           mu0*ring_transmitted(tau,mu0,mu),delta=1e-12)

    def test_smith_matches_lambda_definition_and_limits(self):
        for alpha in (.0025,.005625,.1,.5,1):
            self.assertEqual(smith_g1(0,alpha),0)
            self.assertEqual(smith_g1(1,alpha),1)
            for mu in (1e-6,.001,.01,.1,.5,.99):
                lam=(math.sqrt(1+alpha*alpha*(1-mu*mu)/(mu*mu))-1)/2
                self.assertAlmostEqual(smith_g1(mu,alpha),1/(1+lam),delta=1e-12)
                self.assertTrue(0<=smith_g1(mu,alpha)<=1)
        # Near-smooth water remains visible at grazing, with no global light rescale.
        alpha=.075**2
        self.assertGreater(smith_g1(.1,alpha),.999)
        k=(alpha+1)**2/8
        self.assertLess(.1/(.1*(1-k)+k),.5)

    def test_ggx_peak_and_projected_normalization(self):
        for alpha in (.0025,.075**2,.1,.5,1):
            def distribution(nh):
                denominator=(1-nh*nh)+alpha*alpha*nh*nh
                return alpha*alpha/max(math.pi*denominator*denominator,1e-16)
            self.assertAlmostEqual(distribution(1)*math.pi*alpha*alpha,1,delta=1e-12)
            self.assertTrue(math.isfinite(distribution(1)))
            # Projected solid-angle integral pi*D(sqrt(u))*du, with a slope
            # substitution that resolves the narrow peak; evaluates D itself.
            def integrand(q):
                den=1-q+alpha*alpha*q
                u=(1-q)/den
                return math.pi*distribution(math.sqrt(u))*alpha*alpha/(den*den)
            projected=midpoint_integral(integrand,count=10000)
            self.assertAlmostEqual(projected,1,delta=1e-10)
        alpha=.075**2
        old_peak=alpha*alpha/max(math.pi*alpha**4,1e-8)
        self.assertGreater((1/(math.pi*alpha*alpha))/old_peak,3)

    def test_no_lighting_on_backside_and_reciprocal_masking(self):
        for a in (.0025,.2,1):
            for mu in (.0001,.1,1):
                self.assertEqual(smith_g1(0,a)*smith_g1(mu,a),0)
                for mu0 in (.0001,.2,1):
                    self.assertEqual(smith_g1(mu,a)*smith_g1(mu0,a),
                                     smith_g1(mu0,a)*smith_g1(mu,a))


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--compile-hlsl',action='store_true')
    args=parser.parse_args()
    suite=unittest.TestSuite([unittest.defaultTestLoader.loadTestsFromTestCase(c)
                             for c in (MaterialInvariantTests,OrbitOpticsTests)])
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    compiled=compile_hlsl() if args.compile_hlsl else []
    print(json.dumps({'tests':result.testsRun,'math_pass':result.wasSuccessful(),
                      'standalone_hlsl':compiled,'gpu_or_unreal_proof':False},indent=2))
    sys.exit(0 if result.wasSuccessful() and all(x['pass'] for x in compiled) else 1)

