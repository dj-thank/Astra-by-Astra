from pathlib import Path
import hashlib,json,math,unittest
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[3]
class RegionTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.meta=json.loads((ROOT/'Data/earth_detail_v3.json').read_text(encoding='utf-8'))
  cls.path=ROOT/cls.meta['texture'];cls.im=Image.open(cls.path);cls.a=np.asarray(cls.im)
 def uv(self,lon,lat):return (lon-88)/16,(19-lat)/16
 def test_registration(self):
  self.assertEqual(self.uv(96,11),(.5,.5));self.assertEqual(self.uv(88,19),(0,0));self.assertEqual(self.uv(104,3),(1,1))
  for lon,lat in [(96,11),(90,15),(100,6)]:
   x,y,z=math.cos(math.radians(lat))*math.cos(math.radians(lon)),math.cos(math.radians(lat))*math.sin(math.radians(lon)),math.sin(math.radians(lat))
   self.assertAlmostEqual(math.degrees(math.atan2(y,x)),lon);self.assertAlmostEqual(math.degrees(math.asin(z)),lat)
 def test_geographic_fade(self):
  def weight(lon,lat):
   edge=min(lon-88,104-lon,lat-3,19-lat);t=max(0,min(1,edge));return t*t*(3-2*t)
  self.assertEqual(weight(96,11),1);self.assertEqual(weight(88,11),0);self.assertEqual(weight(87,11),0);self.assertEqual(weight(105,11),0);self.assertEqual(weight(96,20),0);self.assertEqual(weight(88.5,11),.5)
 def test_artifact(self):
  self.assertEqual(self.im.size,(8192,8192));self.assertEqual(self.im.mode,'RGBA');self.assertEqual(hashlib.sha256(self.path.read_bytes()).hexdigest(),self.meta['sha256']);self.assertEqual(len(self.meta['sources']),16)
 def test_nodata_and_initial_coverage(self):
  # Alpha is an availability blend, never classify white cloud as transparent.
  zero=np.all(self.a[:,:,:3]==0,axis=2);self.assertTrue(np.any(zero));self.assertEqual(int(self.a[:,:,3][zero].max()),0)
  self.assertEqual(int(self.a[4096,4096,3]),255)
  self.assertEqual(int(self.a[3072:5120,3072:5120,3].min()),255)
  white=np.all(self.a[3072:5120,3072:5120,:3]>220,axis=2)
  self.assertTrue(np.any(white));self.assertTrue(np.all(self.a[3072:5120,3072:5120,3][white]==255))
 def test_original_rgb_preserved(self):
  for y in range(4):
   for x in range(4):
    path=ROOT/f'work/earth_detail_v3/aqua_2025-09-06_{x}_{y}.png'
    if path.exists():
     source=np.asarray(Image.open(path).convert('RGB'))
     self.assertTrue(np.array_equal(source,self.a[y*2048:(y+1)*2048,x*2048:(x+1)*2048,:3]))
 def test_shader_scope_and_contract(self):
  s=(ROOT/'Shaders/Star/Planet.ush').read_text(encoding='utf-8');a=s.index('if (EarthDetailEnabled');b=s.index('float nightMask')
  self.assertGreater(a,s.index('float rawWater'));self.assertLess(a,b)
  for param in ['EarthDetailTex','EarthDetailEnabled','EarthDetailWest','EarthDetailEast','EarthDetailSouth','EarthDetailNorth','EarthDetailFeatherDegrees','EarthDetailGain']:self.assertIn(param,s)
if __name__=='__main__':unittest.main()
