from pathlib import Path
import json,hashlib
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[3]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
s=json.loads((ROOT/'Data/lunar_panorama_full_region_sources.json').read_text());r=json.loads((ROOT/'Data/lunar_panorama_full_region_recipe.json').read_text());f=json.loads((ROOT/'Content/Star/Data/apollo17_far.json').read_text());v=json.loads((ROOT/'Data/lunar_panorama_far_validation.json').read_text())
for x in s['sources']+s['artifacts']+f['artifacts']:assert sha(ROOT/x['path'])==x['sha256'],x['path']
assert sha(ROOT/r['texturePath'])==r['sha256'];assert Image.open(ROOT/r['texturePath']).size==(8192,8192);assert v['transition']['interiorSamplesUnchanged']==9025;assert v['transition']['maxNewEdgeVsNative5mMeters']<.5;assert f['residentBytes']<145*1024*1024
for x in s['registrationSummary']:
 assert x['independentPhoto']!=x['frame'] and x['independentPhotoSamples']>5
 assert x['independentPhotoRmsDegrees']<.6 and x['wrongHeading10DegreesRms']>x['pointHoldoutRmsDegrees']*3
assert len(s['cpuRender']['views'])==13
print(json.dumps(dict(status='LOCAL_FULL_REGION_ASSET_AND_SAMPLING_CHECKS_PASS',sources=len(s['sources']),artifacts=len(s['artifacts']),renderViews=13,full360PhotoComplete=False,UEPass=False)))
