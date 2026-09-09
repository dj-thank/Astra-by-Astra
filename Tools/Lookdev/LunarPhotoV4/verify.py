"""Verify real scan provenance, mask exclusions and projector axis conventions."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, required=True)
parser.add_argument('--root', type=Path, default=Path.cwd())
args = parser.parse_args()
root, source = args.root.resolve(), args.source_root.resolve()
recipe = json.loads((root/'Data/lunar_photo_display_v04.json').read_text(encoding='utf-8'))
failures, checks = [], []

def check(name, condition, detail):
    (checks if condition else failures).append(dict(check=name, detail=detail))

for record in recipe['sources']:
    actual = hashlib.sha256((source/record['path']).read_bytes()).hexdigest()
    check('source unchanged '+record['id'], actual == record['sha256'], actual)
for record in recipe['artifacts']:
    actual = hashlib.sha256((root/record['path']).read_bytes()).hexdigest()
    check('artifact hash '+record['path'], actual == record['sha256'], actual)

rgba=np.asarray(Image.open(root/recipe['primaryTexture']))
ids=np.asarray(Image.open(root/recipe['alpha']['sourceIdTexture']))
height,width=rgba.shape[:2]
alpha=rgba[...,3]
az=(np.arange(width)+.5)/width*360
el=25-(np.arange(height)+.5)/height*45
check('expected 16K RGBA',rgba.shape==(2048,16384,4),list(rgba.shape))
check('independent alpha matches', np.array_equal(alpha,np.asarray(Image.open(root/recipe['alpha']['independentCoverageTexture']))),'byte equality')
check('direct Sun not terrain', not np.any(alpha[:,(az>82)&(az<104)]),'east 82..104deg alpha zero')
check('LM occluded sector not terrain',not np.any(alpha[:,(az>195)&(az<209)]),'195..209deg alpha zero')
check('near ground excluded',not np.any(alpha[el<-.15]),'below -0.15deg alpha zero')
excluded=[int(n) for n in recipe['alpha']['excludedFullFrames']]
check('no excluded source contributions',not np.any(np.isin(ids[alpha>0],excluded)),excluded)
check('observation support exists',np.all(ids[alpha>0]>0),'all visible texels have original frame ID')
for (east,north),expected in [((0,1),0),((1,0),.25),((0,-1),.5),((-1,0),.75)]:
    actual=np.arctan2(east,north)/(2*np.pi)%1
    check('ENU cardinal '+str((east,north)),abs(actual-expected)<1e-12,actual)
check('lighting mismatch retained',recipe['lighting']['lightingMatched'] is False,recipe['lighting']['currentGameSunElevationDegrees'])
check('not claimed 360',recipe['alpha']['complete360PhotoCoverage'] is False,recipe['alpha']['missingAzimuthRangesDegrees'])
check('not claimed 2km walk',recipe['projector']['arbitraryTwoKilometerWalkCoverage'] is False,recipe['projector']['walkFadeRadiusMeters'])
result=dict(status='SOURCE_CHECKS_PASS' if not failures else 'SOURCE_CHECKS_FAIL',checks=checks,failures=failures,
            limitations=['CPU artifact validation only; not UE or packaged EVA acceptance',
                         'Skyline transfer is a placement operation, not independent surveyed accuracy'])
(root/'Tools/Lookdev/LunarPhotoV4/validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')
print(json.dumps(dict(status=result['status'],checks=len(checks),failures=failures),indent=2))
raise SystemExit(bool(failures))
