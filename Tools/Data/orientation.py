"""Export fixed-epoch body-frame rotations using public NASA NAIF SPICE kernels."""
import json
import shutil
import numpy as np
import spiceypy as spice
from acquire import ROOT, RAW, digest, save_json
from horizons import EPOCH

def main():
    kernels=['naif0012.tls','pck00011.tpc','moon_pa_de421_1900-2050.bpc','moon_080317.tf']
    for name in kernels: spice.furnsh(str(RAW/name))
    et=spice.str2et(EPOCH)
    path=ROOT/'Content/Star/Data/bodies.json';data=json.loads(path.read_text(encoding='utf-8'))
    for body in data['bodies']:
        frame='MOON_ME' if body['id']=='moon' else 'IAU_'+body['id'].upper()
        rotation=spice.pxform(frame,'ECLIPJ2000',et)
        assert np.allclose(rotation.T@rotation,np.eye(3),atol=1e-13) and abs(np.linalg.det(rotation)-1)<1e-13
        body['bodyFixedFrame']=frame
        body['bodyFixedToEclipticJ2000']=rotation.tolist()
        body['orientationConvention']='row-major 3x3 matrix multiplying column vectors; local +X lon0 lat0, +Y lon90East lat0, +Z north'
        body['northPoleEclipticJ2000']=rotation[:,2].tolist()
        body['orientationEpoch']=EPOCH
        body['orientationKernels']=kernels if body['id']=='moon' else kernels[:2]
        if body['id']=='earth':body['orientationLimit']='IAU_EARTH approximate spin model, not ITRF high-precision Earth orientation.'
        if body['id']=='moon':body['orientationLimit']='DE421 mean Earth/polar-axis frame, consistent with conventional LRO mapping; position still Horizons current solution.'
    data['orientationCalculation']={'library':'SpiceyPy '+spice.__version__,'toolkit':spice.tkvrsn('TOOLKIT'),
                                    'ephemerisTimeTdbSecondsPastJ2000':et,'operation':'pxform(bodyFixedFrame,ECLIPJ2000,ET)'}
    data['orientationSources']=[]
    for name in kernels:
        target=ROOT/'Data/provenance/kernels'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(RAW/name,target)
        receipt=json.loads((RAW/(name+'.receipt.json')).read_text(encoding='utf-8'))
        data['orientationSources'].append({'path':target.relative_to(ROOT).as_posix(),**receipt})
    save_json(path,data);spice.kclear()
    print('Body orientations',[(x['id'],x['bodyFixedFrame'],x['northPoleEclipticJ2000']) for x in data['bodies']])

if __name__=='__main__':main()
