"""Offline runtime-artifact checks plus exact source conversion checks when raw bytes exist."""
import json
import math
import unittest
from pathlib import Path
import numpy as np
from PIL import Image
import imagecodecs
import tifffile
from acquire import ROOT, RAW, digest
from sample import sample_global, sample_apollo, apollo_pixel, sky_uv

def read(name):return json.loads((ROOT/name).read_text(encoding='utf-8'))

class SamplerTests(unittest.TestCase):
    def test_global_seam_and_centers(self):
        heights=np.array([[2,4,6,8],[10,12,14,16]],dtype='<i2')
        self.assertEqual(sample_global(heights,-135,45),1.0)
        self.assertEqual(sample_global(heights,135,-45),8.0)
        self.assertEqual(sample_global(heights,180,45),2.5)
        self.assertEqual(sample_global(heights,-180,45),sample_global(heights,540,45))
        self.assertEqual(sample_global(heights,-135,90),1.0)
        with self.assertRaises(ValueError):sample_global(heights,float('nan'),0)

    def test_exact_unsigned_offset_control(self):
        source=np.array([0,19999,20000,20001,40000],dtype=np.uint16)
        decoded=(source.astype(np.int32)-20000).astype('<i2')*0.5
        self.assertEqual(decoded.tolist(),[-10000,-0.5,0,0.5,10000])

    def test_sky_direction_alignment(self):
        self.assertTrue(np.allclose(sky_uv([1,0,0]),[0.5,0.5],atol=1e-12))
        self.assertTrue(np.allclose(sky_uv([-1,0,0]),[0,0.5],atol=1e-12))
        eps=math.radians(23.439291111111)
        self.assertTrue(np.allclose(sky_uv([0,math.cos(eps),-math.sin(eps)]),[0.25,0.5],atol=1e-12))
        self.assertAlmostEqual(sky_uv([0,math.sin(eps),math.cos(eps)])[1],0,places=7)

class ArtifactTests(unittest.TestCase):
    def test_manifest_integrity_and_coverage(self):
        manifest=read('Data/manifest.json');paths=set()
        for asset in manifest['assets']:
            path=ROOT/asset['path'];self.assertTrue(path.is_file());self.assertEqual(digest(path),asset['sha256'])
            self.assertEqual(path.stat().st_size,asset['bytes']);paths.add(asset['path'])
            for field in ['observationDate','units','datum','colorSpace','process','credit']:self.assertTrue(asset[field])
            self.assertTrue(asset['sources'])
        # Engine derivatives and the gameplay assembly index have separate
        # import/runtime checks; this manifest describes acquired science data.
        files={p.relative_to(ROOT).as_posix() for base in ['Content/Star/Data','Content/Star/Textures'] for p in (ROOT/base).glob('*')
               if p.is_file() and p.suffix not in ('.uasset','.uexp','.ubulk') and p.name!='runtime_assets.json'}
        self.assertEqual(files,paths)

    def test_horizons_and_orientation(self):
        data=read('Content/Star/Data/bodies.json');self.assertEqual(data['epoch'],read('Data/epoch_selection.json')['selection']['selectedEpoch'])
        self.assertEqual(data['origin'],'solar-system-barycenter');b={x['id']:x for x in data['bodies']}
        for body in b.values():
            matrix=np.array(body['bodyFixedToEclipticJ2000']);self.assertTrue(np.allclose(matrix.T@matrix,np.eye(3),atol=1e-12))
            self.assertAlmostEqual(np.linalg.det(matrix),1,places=12)
            self.assertEqual(body['basis'],'ecliptic-j2000');self.assertEqual(len(body['positionMeters']),3)
        earth=np.array(b['earth']['positionMeters']);moon=np.array(b['moon']['positionMeters']);sun=np.array(b['sun']['positionMeters'])
        self.assertTrue(350e6<np.linalg.norm(earth-moon)<410e6)
        self.assertTrue(145e9<np.linalg.norm(earth-sun)<153e9)
        self.assertEqual(b['moon']['radiusMeters'],1737400)
        moon_x=np.array(b['moon']['bodyFixedToEclipticJ2000'])[:,0]
        self.assertGreater(np.dot(moon_x,(earth-moon)/np.linalg.norm(earth-moon)),0.95)
        self.assertTrue(0.87<b['saturn']['northPoleEclipticJ2000'][2]<0.90)
        for source in data['provenance']+data['orientationSources']:
            self.assertEqual(digest(ROOT/source['path']),source['sha256'])

    def test_global_dem_byte_count_and_physical_range(self):
        m=read('Content/Star/Data/moon_ldem_16.json');a=np.fromfile(ROOT/m['binaryPath'],dtype='<i2')
        self.assertEqual(len(a),m['width']*m['height']);self.assertEqual(a.min()*0.5,m['minHeightMeters'])
        self.assertEqual(a.max()*0.5,m['maxHeightMeters']);self.assertTrue(-12000<a.min()*0.5<-5000)
        self.assertTrue(8000<a.max()*0.5<12000)
        elevation=sample_global(a.reshape(m['height'],m['width']),30.7717,20.1908)
        self.assertTrue(-3500<elevation<-1500)

    def test_apollo_registration_and_invalid_fallback(self):
        m=read('Content/Star/Data/apollo17.json');shape=(m['height'],m['width'])
        a=np.fromfile(ROOT/m['binaryPath'],dtype='<f4').reshape(shape);mask=np.fromfile(ROOT/m['validityPath'],dtype='u1').reshape(shape)
        self.assertTrue(np.isfinite(a).all());self.assertTrue(set(np.unique(mask)).issubset({0,1}))
        self.assertAlmostEqual(float(mask.mean()),m['validFraction'],places=10)
        lon,lat=30.7717,20.1908;x,y=apollo_pixel(m,lon,lat)
        self.assertAlmostEqual(x,(lon-m['westLongitudeDegrees'])/(m['eastLongitudeDegrees']-m['westLongitudeDegrees'])*m['width']-.5,places=7)
        self.assertAlmostEqual(y,(m['northLatitudeDegrees']-lat)/(m['northLatitudeDegrees']-m['southLatitudeDegrees'])*m['height']-.5,places=7)
        self.assertTrue(-3500<sample_apollo(a,mask,m,lon,lat)<-1500)
        self.assertIsNone(sample_apollo(a,mask,m,0,0))
        invalid=mask.copy();invalid[int(y),int(x)]=0
        self.assertIsNone(sample_apollo(a,invalid,m,lon,lat))
        with Image.open(ROOT/m['texturePath']) as image:self.assertEqual(image.size,(m['width'],m['height']))

    def test_runtime_texture_dimensions_and_ring_profile(self):
        expected={'moon_albedo_8k.png':(8192,4096),'earth_day_8k.jpg':(8192,4096),'earth_night_8k.jpg':(8192,4096),
                  'saturn_body_reference.png':(4096,2048),'saturn_rings_rgba.png':(4096,16)}
        for f,size in expected.items():
            with Image.open(ROOT/'Content/Star/Textures'/f) as image:self.assertEqual(image.size,size)
        p=np.loadtxt(ROOT/'Content/Star/Data/saturn_ring_profile.csv',delimiter=',',skiprows=1)
        self.assertEqual(p.shape,(71007,4));self.assertTrue(np.all(np.diff(p[:,0])==1000))
        self.assertTrue(p[0,0]<74491000 and p[-1,0]>140000000)

    def test_exact_source_to_runtime_dem(self):
        if not (RAW/'ldem_16_uint.tif').exists():self.skipTest('Raw acquisition cache not present in this checkout')
        source=tifffile.imread(RAW/'ldem_16_uint.tif').astype(np.int32)-20000
        runtime=np.fromfile(ROOT/'Content/Star/Data/moon_ldem_16_i16.bin',dtype='<i2').reshape(source.shape)
        self.assertTrue(np.array_equal(source,runtime))
        m=read('Content/Star/Data/apollo17.json');col,row,w,h=m['sourceWindow']
        raw=tifffile.imread(RAW/'NAC_DTM_APOLLO17.TIF')[row:row+h,col:col+w]
        runtime=np.fromfile(ROOT/m['binaryPath'],dtype='<f4').reshape(h,w);mask=np.fromfile(ROOT/m['validityPath'],dtype='u1').reshape(h,w).astype(bool)
        self.assertTrue(np.array_equal(raw[mask],runtime[mask]))

    def test_moon_rgb16_lossless(self):
        if not (RAW/'lroc_color_16bit_srgb_8k.tif').exists():self.skipTest('Raw acquisition cache not present in this checkout')
        rgb=tifffile.imread(RAW/'lroc_color_16bit_srgb_8k.tif')
        output=imagecodecs.png_decode((ROOT/'Content/Star/Textures/moon_albedo_8k.png').read_bytes())
        self.assertEqual(output.dtype,np.uint16);self.assertTrue(np.array_equal(rgb,output))

if __name__=='__main__':unittest.main(verbosity=2)
