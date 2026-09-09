"""Validate actual source/mesh/camera bytes and a wrong-heading negative control."""
import json,hashlib
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.spatial.transform import Rotation
from scipy.ndimage import map_coordinates
ROOT=Path(__file__).resolve().parents[3]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 s=json.loads((ROOT/'Data/lunar_panorama_sources.json').read_text(encoding='utf-8'));r=json.loads((ROOT/'Data/lunar_panorama_registration.json').read_text(encoding='utf-8'))
 for item in s['sources']+s['artifacts']:assert digest(ROOT/item['path'])==item['sha256'],item['path']
 assert len(s['sources'])==27
 for c in r['refinedCameras']:
  R=np.array(c['cameraToEastNorthUp']);assert np.allclose(R.T@R,np.eye(3),atol=2e-6);assert abs(np.linalg.det(R)-1)<2e-6
 c=next(x for x in r['refinedCameras'] if x['frame']==22500);assert c['holdoutRmsDeg']<.15
 h=np.load(ROOT/'Content/Star/Art/LunarPanorama/Registration/dem_horizon.npz');px=np.c_[np.array(c['skylinePixels']),np.ones(len(c['skylinePixels']))];v=px@np.linalg.inv(c['K']).T;v=v@np.array(c['cameraToEastNorthUp']).T;v/=np.linalg.norm(v,axis=1)[:,None]
 wrong=v@Rotation.from_euler('z',-10,degrees=True).as_matrix().T;a=np.degrees(np.arctan2(wrong[:,0],wrong[:,1]))%360;e=np.degrees(np.arcsin(wrong[:,2]));wrongRms=float(np.sqrt(np.mean((e-np.interp(a,h['az'],h['elevation'],period=360))**2)));assert wrongRms>c['holdoutRmsDeg']*3
 obj=ROOT/'Content/Star/Art/LunarPanorama/north_massif_measured_enu_m.obj';vertices=np.array([list(map(float,l.split()[1:])) for l in obj.read_text(encoding='utf-8').splitlines() if l.startswith('v ')]);assert len(vertices)==53360;assert np.ptp(vertices[:,2])>900
 # Recompute source DEM -> spherical ENU for sampled vertices independently.
 meta=json.loads((ROOT/'Content/Star/Data/apollo17.json').read_text());height=np.fromfile(ROOT/meta['binaryPath'],dtype='<f4').reshape(2400,2800);rad=1737400;la0,lo0=np.deg2rad([20.1908,30.7717]);left,top=meta['topLeftPixelCornerProjectedMeters'];sp=meta['pixelSpacingMeters'];x0=rad*np.cos(np.deg2rad(20))*(lo0-np.pi);y0=rad*la0;h0=float(map_coordinates(height,[[(top-y0)/sp-.5],[(x0-left)/sp-.5]],order=1)[0])
 err=[]
 for idx in np.linspace(0,len(vertices)-1,79,dtype=int):
  row=80+(idx//290)*5;col=350+(idx%290)*5;lat=(top-(row+.5)*sp)/rad;lon=np.pi+(left+(col+.5)*sp)/(rad*np.cos(np.deg2rad(20)));radius=rad+float(height[row,col]);dl=lon-lo0
  expected=np.array([radius*np.cos(lat)*np.sin(dl),radius*(np.sin(lat)*np.cos(la0)-np.cos(lat)*np.sin(la0)*np.cos(dl)),radius*(np.sin(lat)*np.sin(la0)+np.cos(lat)*np.cos(la0)*np.cos(dl))-(rad+h0)])
  err.append(float(np.linalg.norm(vertices[idx]-expected)))
 assert max(err)<1e-5
 review=ROOT/'Content/Star/Art/LunarPanorama/Review';a=np.asarray(Image.open(review/'reference.png')).astype(float);b=np.asarray(Image.open(review/'glb_readback.png')).astype(float);diff=abs(a-b);assert diff.mean()<.1
 result=dict(status='LOCAL_SOURCE_GEOMETRY_REGISTRATION_CHECKS_PASS',sourceHashChecks=27,artifactHashChecks=len(s['artifacts']),source22500HoldoutRmsDegrees=c['holdoutRmsDeg'],wrongHeading10DegreesRms=wrongRms,demVertexCheckCount=len(err),demVertexMaxErrorMeters=max(err),meshVerticalReliefMeters=float(np.ptp(vertices[:,2])),glbReadbackMeanByteDifference=float(diff.mean()),glbReadbackMaxByteDifference=float(diff.max()),notValidated=['UE','GPU','packaged game','independent visual acceptance','true camera location','multi-location photographic accuracy'])
 (ROOT/'Data/lunar_panorama_validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
