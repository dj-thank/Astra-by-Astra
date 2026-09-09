"""Unmodified RGB crop on one bounded ground patch; no inferred camera pose."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=Path,required=True)
args=parser.parse_args()
root=Path.cwd()
source=args.source_root.resolve()
relative='Content/Star/Art/LunarPanorama/Source/AS17-147-22501HR.jpg'
original=source/relative
box=(850,820,1520,1040)
crop=np.asarray(Image.open(original).convert('RGB').crop(box))
h,w=crop.shape[:2]
y,x=np.indices((h,w))
edge=np.minimum.reduce([x,y,w-1-x,h-1-y]).astype(np.float32)
t=np.clip(edge/12,0,1)
alpha=np.round(255*t*t*(3-2*t)).astype(np.uint8)
rgba=np.dstack([crop,alpha])
out=root/'Content/Star/Art/LunarPhotoV4/GroundPatch'
out.mkdir(parents=True,exist_ok=True)
texture=out/'AS17-147-22501_ground_patch_rgba.png'
Image.fromarray(rgba).save(texture)
reg=json.loads((source/'Data/lunar_panorama_full_region_registration.json').read_text(encoding='utf-8'))
c=next(c for c in reg['cameras'] if c['frame']==22500)
R=np.array(c['cameraToEastNorthUp'])
forward=R[:2,2].copy()
forward/=np.linalg.norm(forward)
right=np.array([forward[1],-forward[0]])
center=forward*1.8
width=2.0
depth=width*h/w
matrix=np.array([[right[0]/width,right[1]/width,0,.5-np.dot(center,right)/width],
                 [-forward[0]/depth,-forward[1]/depth,0,.5+np.dot(center,forward)/depth]])
corners=[(center+sx*right*width/2+sy*forward*depth/2).tolist() for sx,sy in [(-1,1),(1,1),(1,-1),(-1,-1)]]
source_manifest=json.loads((source/'Data/lunar_panorama_sources.json').read_text(encoding='utf-8'))
record=next(s for s in source_manifest['sources'] if s['id']=='AS17-147-22501')
source_sha=hashlib.sha256(original.read_bytes()).hexdigest()
assert source_sha==record['sha256']
assert np.array_equal(np.asarray(Image.open(texture))[...,:3],crop)
uv=np.c_[np.array(corners),np.zeros(4),np.ones(4)]@matrix.T
assert np.allclose(uv,[[0,0],[1,0],[1,1],[0,1]],atol=1e-12)
recipe=dict(schemaVersion=1,status='LOCAL_ART_PLACEMENT_AB_CANDIDATE_NOT_GEOGRAPHIC_PHOTO_REGISTRATION',
    texturePath=texture.relative_to(root).as_posix(),pixels=[w,h],source=record,sourceCropXYXY=list(box),
    rgb='Exact decoded original RGB bytes, sRGB display interpretation; no resize, color change, noise, tiling, inpaint or reconstruction.',
    alpha='Straight linear alpha; only outer 12 pixels feathered, interior 255; border 0. RGB beneath alpha unchanged.',
    cameraEvidence=dict(registeredFrame=22500,sourceFrame22501HasExistingCameraSolution=False,
        reused='Only horizontal direction from existing 22500 cameraToEastNorthUp.',
        unmeasured='22501 pose, actual crop position, physical dimensions and ground homography are unsolved. This is one explicit art-placement candidate, not a recovered location.'),
    placement=dict(frame='Fixed lunar ENU meters relative to LM proxy lat20.1908 lon30.7717. U=0 is proxy source ground; texture mapping ignores U.',
        azimuthDegrees=float(np.degrees(np.arctan2(forward[0],forward[1]))%360),
        centerEastNorthMeters=center.tolist(),widthRightMeters=width,depthForwardMeters=depth,
        assumedDimensions=True,forwardEastNorth=forward.tolist(),rightEastNorth=right.tolist(),
        cornersEastNorthMetersTLTRBRBL=corners,
        fixedENUToUVMatrix2x4=matrix.tolist(),formula='float2 uv = Matrix2x4 * float4(E,N,U,1); accept only 0<=uv.x<=1 and 0<=uv.y<=1; NEVER frac/repeat.',
        sourceSourcePlane='Lay on the existing measured ground; do not create new terrain or photograph-derived height.',
        rootGroundUV2Convention='If root UV2.y is U from photographic camera, ground-level local U=UV2.y+1.6. Mapping matrix ignores U; use this only for projection-height clipping.',
        heightClipMetersFromLocalGround=0.20,viewUse='Ground-facing points only; dot(normal,localUp)>0.85.',
        cameraDistanceFadeMeters=[4,7],nonRepeating=True,
        moveOrScale='To test another single patch, change center/width/depth explicitly and recompute rows. Do not tile.'),
    lighting=dict(bakedOriginalSun=True,matchedToCurrent29_393Degrees=False,
        display='A/B photo-emissive branch or final radiance blend; avoid double solar lighting. Alpha feather reveals ordinary ground around the patch.'),
    validation=dict(sourceSha256=source_sha,outputSha256=hashlib.sha256(texture.read_bytes()).hexdigest(),
        rgbByteEquality=True,uvCornersRoundTrip=True,sourceRegistrationClaim=False,
        limit='670x220 pixels and visible optical blur; only useful as a narrow A/B candidate, not detailed close-up coverage.'),
    credit=source_manifest['rights']['credit'],observation=source_manifest['sourceSequence'])
(out/'placement.json').write_text(json.dumps(recipe,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
print(json.dumps(dict(texture=str(texture),azimuth=recipe['placement']['azimuthDegrees'],corners=uv.tolist(),rgbByteEquality=True),indent=2))
