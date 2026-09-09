"""CPU controls for photo registration/negative shadows; never GPU acceptance."""
import json
import unittest
from pathlib import Path
import numpy as np
from PIL import Image
from build import ROOT, OUT, RAW, sha, RAW_SHA


class PhotoControls(unittest.TestCase):
    def test_raw_photo_and_crop_pixels_preserved(self):
        self.assertEqual(sha(RAW/'AS11-45-6705A.tif'),RAW_SHA)
        source=np.array(Image.open(RAW/'AS11-45-6705A.tif').convert('RGB'))
        crop=np.array(Image.open(OUT/'as11_6705a_observed_crop.png'))
        np.testing.assert_array_equal(crop,source[120:1220,650:1750])

    def test_georegistration_pixel_center_roundtrip(self):
        m=json.loads((ROOT/'Content/Star/Data/apollo17.json').read_text(encoding='utf-8'))
        for col,row in [(0,0),(2799,2399),(1400,1200),(120,2300)]:
            lon=m['westLongitudeDegrees']+(col+.5)/2800*(m['eastLongitudeDegrees']-m['westLongitudeDegrees'])
            lat=m['northLatitudeDegrees']-(row+.5)/2400*(m['northLatitudeDegrees']-m['southLatitudeDegrees'])
            u,v=(lon+180)/360,(90-lat)/180
            shader_lon=u*360-180;shader_lat=90-v*180
            self.assertAlmostEqual((shader_lon-30.52603930572124)/.491320668932333*2800-.5,col,places=7)
            self.assertAlmostEqual((20.388743042972354-shader_lat)/.395734634601687*2400-.5,row,places=7)

    def test_deep_photo_shadows_and_saturation_contribute_zero(self):
        src=np.asarray(Image.open(ROOT/'Content/Star/Textures/apollo17_ortho_5m.png'))[:,:,0]/255
        reg=np.asarray(Image.open(OUT/'apollo17_photo_regional_linear.png'))/255
        reject=(src<=.10)|(src>=.99)
        self.assertGreater(reject.sum(),1000)
        np.testing.assert_array_equal(reg[:,:,1][reject],0)
        # A reversed light cannot expose source shadow in these rejected cells:
        # the albedo multiplier stays 1 for either positive or negative residual.
        for sign in [-1,1]:
            modulation=1+sign*(reg[:,:,0]*2-1)*reg[:,:,1]*.65
            np.testing.assert_array_equal(modulation[reject],1)
        self.assertLessEqual(np.max(np.abs((reg[:,:,0]*2-1)*reg[:,:,1]*.65)),.212)

    def test_near_scale_normal_and_edges(self):
        recipe=json.loads((ROOT/'Tools/Lookdev/PhotoMoon/material_recipe.json').read_text(encoding='utf-8'))
        self.assertTrue(26<recipe['scalars']['DetailScale']<27)
        normal=np.asarray(Image.open(OUT/'apollo_soil_normal_dx.png'),dtype=float)/255*2-1
        self.assertTrue(np.all(normal[:,:,2]>.95))
        self.assertLess(np.max(abs(np.linalg.norm(normal,axis=-1)-1)),.009)
        detail=np.array(Image.open(OUT/'apollo_soil_detail_linear.png'))
        self.assertTrue(np.all(detail[0,:,0]==128))
        self.assertTrue(np.all(detail[-1,:,0]==128))
        self.assertTrue(np.all(detail[:,0,0]==128))
        self.assertTrue(np.all(detail[:,-1,0]==128))

    def test_no_photo_outside_region(self):
        for uv in [(-.1,.5),(1.1,.5),(.5,-.1),(.5,1.1),(0,.5),(1,.5)]:
            edge=min(*uv,1-uv[0],1-uv[1])
            t=np.clip(edge/.025,0,1)
            self.assertEqual(t*t*(3-2*t),0)

    def test_pot_runtime_products_and_shadow_support(self):
        reg=np.asarray(Image.open(OUT/'apollo17_photo_regional_4096_linear.png'))
        self.assertEqual(reg.shape,(4096,4096,4))
        source=np.asarray(Image.open(ROOT/'Content/Star/Textures/apollo17_ortho_5m.png'))[:,:,0]/255
        reject=(source<=.10)|(source>=.99)
        reject=np.asarray(Image.fromarray(reject).resize((4096,4096),Image.Resampling.NEAREST))
        self.assertTrue(np.all(reg[:,:,1][reject]==0))
        self.assertTrue(np.all(reg[:,:,0][reject]==128))
        normal=np.asarray(Image.open(OUT/'apollo_soil_normal_1024_dx.png'),dtype=float)/255*2-1
        self.assertEqual(normal.shape,(1024,1024,3))
        self.assertLess(np.max(abs(np.linalg.norm(normal,axis=-1)-1)),.009)
        self.assertTrue(np.all(normal[:,:,2]>.95))
        detail=np.asarray(Image.open(OUT/'apollo_soil_detail_1024_linear.png'))
        self.assertEqual(detail.shape,(1024,1024,3))
        recipe=json.loads((ROOT/'Tools/Lookdev/PhotoMoon/material_recipe.json').read_text(encoding='utf-8'))
        self.assertAlmostEqual(1/recipe['scalars']['DetailScale'],.03818088001175898,places=12)


if __name__=='__main__':
    unittest.main()
