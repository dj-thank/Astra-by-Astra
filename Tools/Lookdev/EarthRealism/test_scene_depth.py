"""Independent projective-depth geometry checks for inside-atmosphere rendering."""
import math,unittest
import numpy as np
R=6371008.4

def reconstructed_distance(scene_z,pixel_z,pixel_distance,render_radius):
    # Float32 arithmetic mirrors shader precision; expected values below come
    # from independently chosen physical distances and camera projection.
    return float((np.float32(scene_z)/np.float32(pixel_z))*np.float32(pixel_distance)/np.float32(render_radius))

class AtmosphereDepthTests(unittest.TestCase):
    def test_inside_shell_is_hidden_by_ordinary_depth_test(self):
        camera=R+35000.0;outer=R+100000.0
        ground_distance=camera-R
        back_face_distance=camera+outer
        self.assertGreater(back_face_distance,ground_distance)
        # The ray contains 35 km of air even though the back face is occluded.
        self.assertGreater(ground_distance,0)
    def test_view_z_is_not_radial_distance(self):
        physical=80000.0;cosine=.25;proxy=.01;shell=8e6
        z=physical*proxy*100*cosine
        got=reconstructed_distance(z,shell*proxy*100*cosine,shell*proxy*100,R*proxy*100)*R
        self.assertAlmostEqual(got,physical,delta=.02)
        self.assertGreater(abs(z/(proxy*100)-physical),50000)
    def test_scale_and_off_axis_invariance(self):
        for distance in [5.0,50.0,35000.0,160000.0,2e6]:
            for proxy in [1.0,.01,.0001]:
                for cosine in [1.0,.7,.2]:
                    shell=13e6
                    got=reconstructed_distance(distance*proxy*100*cosine,shell*proxy*100*cosine,shell*proxy*100,R*proxy*100)*R
                    self.assertAlmostEqual(got,distance,delta=max(.0001,distance*3e-7))
    def test_foreground_cuts_air_column_before_cockpit(self):
        h=35000.0;scale=8000.0
        column=lambda d:scale*(math.exp(-(h-d)/scale)-math.exp(-h/scale))
        foreground=column(10.0);ground=column(h)
        self.assertLess(foreground/ground,2e-5)
        # For an outside observer, a ship before atmosphere entry has no segment.
        entry=160000.0-100000.0
        self.assertLessEqual(min(160000.0,10.0)-entry,0)
if __name__=='__main__':unittest.main(verbosity=2)
