"""Verify serialized assets, physical integrals, source pixels and registration."""
from pathlib import Path
import sys,json,unittest
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'work/earth-v2/deps'))
sys.path.insert(0,str(ROOT/'Tools/Unreal/Materials'))
import OpenEXR
import build_numeric_textures as numeric
from create_materials import MATERIAL_SPECS

class EarthV2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        channels=OpenEXR.File(str(ROOT/'Content/Star/Art/EarthV2/T_EarthOpticalDepth.exr'),separate_channels=True).channels()
        cls.lut=np.stack([channels['R'].pixels,channels['G'].pixels],axis=-1)
    def test_serialized_lut_vertical_integrals(self):
        h=100000*(np.arange(256)/255)**2
        expected=np.stack([scale*(np.exp(-h/scale)-np.exp(-100000/scale))/1e6 for scale in (8000.,1200.)],axis=-1).astype(np.float16).astype(np.float32)
        np.testing.assert_allclose(self.lut[:,-1,:],expected,atol=1e-9,rtol=0.001)
    def test_serialized_lut_against_dense_independent_rays(self):
        rng=np.random.default_rng(18203)
        h=rng.uniform(0,1,1000)**2*100000
        horizon=-np.sqrt(1-(numeric.R/(numeric.R+h))**2)
        mu=horizon+(1-horizon)*rng.uniform(0,1,1000)**3
        expected=numeric.columns(h,mu,512)
        actual=numeric.sample(self.lut,h,mu)
        beta=np.array([[5.802e-6,4.3956e-6],[13.558e-6,4.3956e-6],[33.1e-6,4.3956e-6]])
        self.assertLess(np.max(np.abs(np.exp(-expected@beta.T)-np.exp(-actual@beta.T))),0.001)
    def test_focused_view_integrator_against_dense_reference(self):
        import math
        from view_quadrature_reference import integrate
        for radius in (1.0158,1.04,1.07,3.9):
            camera=np.array([0.0,0.0,radius])
            for angle in (0.0,math.asin(0.5/radius),math.asin(0.9/radius)):
                ray=np.array([math.sin(angle),0.0,-math.cos(angle)])
                for sun_angle in (0,30,60,90):
                    sun=np.array([math.sin(math.radians(sun_angle)),0.0,math.cos(math.radians(sun_angle))])
                    dense=integrate(camera,ray,sun,4096,False)
                    focused=integrate(camera,ray,sun,16,True)
                    self.assertLess(np.max(np.abs(focused-dense))/max(np.max(dense),1e-7),0.01)

    def test_reference_radius_matches_game(self):
        bodies=json.loads((ROOT/'Content/Star/Data/bodies.json').read_text(encoding='utf-8'))['bodies']
        self.assertEqual(next(x['radiusMeters'] for x in bodies if x['id']=='earth'),numeric.R)
    def test_noise_atlas_periodic_borders(self):
        atlas=np.asarray(Image.open(ROOT/'Content/Star/Art/EarthV2/T_CloudDensityAtlas.png'))
        self.assertEqual(atlas.shape,(528,528))
        for z in range(64):
            x=z%8;y=z//8;tile=atlas[y*66:(y+1)*66,x*66:(x+1)*66]
            np.testing.assert_array_equal(tile[0,1:-1],tile[-2,1:-1])
            np.testing.assert_array_equal(tile[-1,1:-1],tile[1,1:-1])
            np.testing.assert_array_equal(tile[1:-1,0],tile[1:-1,-2])
            np.testing.assert_array_equal(tile[1:-1,-1],tile[1:-1,1])
    def test_pacific_pixels_are_source_crop(self):
        photo=Image.open(ROOT/'Content/Star/Art/EarthV2/T_Pacific_Aqua_20250906_8K.png').convert('RGB')
        self.assertEqual(photo.size,(4096,8192))
        for y in range(4):
            for x in (2,3):
                raw=Image.open(ROOT/f'work/earth-v2/pacific-raw/aqua_2025-09-06_{x}_{y}.png').convert('RGB')
                crop=photo.crop(((x-2)*2048,y*2048,(x-1)*2048,(y+1)*2048))
                np.testing.assert_array_equal(np.asarray(crop),np.asarray(raw))
    def test_registration_matches_materials(self):
        meta=json.loads((ROOT/'Data/earth_v2_pacific.json').read_text(encoding='utf-8'))
        for material in ('M_Planet','M_Clouds','M_CloudsSurface'):
            for side in ('west','east','south','north'):
                self.assertEqual(MATERIAL_SPECS[material]['scalars']['Pacific'+side.title()],meta['bounds'][side])
        self.assertLess(meta['missingFraction'],0.00001)

if __name__=='__main__':unittest.main(verbosity=2)
