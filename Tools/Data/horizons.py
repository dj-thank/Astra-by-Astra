"""Actual fixed-epoch Horizons vectors, SI meters, geometric ICRF/ecliptic J2000."""
import csv
import json
import shutil
import requests
from acquire import ROOT, RAW, save_json, digest

EPOCH = "2026-09-06T00:00:00Z"
PARAMS = dict(format="json", CENTER="'500@0'", EPHEM_TYPE="'VECTORS'", TIME_TYPE="'UT'",
              START_TIME="'2026-09-06 00:00:00'", STOP_TIME="'2026-09-06 00:01:00'", STEP_SIZE="'1 min'",
              REF_SYSTEM="'ICRF'", REF_PLANE="'ECLIPTIC'", OUT_UNITS="'KM-S'", VEC_TABLE="'2'",
              VEC_CORR="'NONE'", CSV_FORMAT="'YES'", OBJ_DATA="'YES'")

def main():
    # The current scenario is replayed from its exact acquired response record.
    # Never relabel the older committed September 6 cache as a new epoch.
    if (ROOT/'Data/epoch_selection.json').exists():
        from select_epoch import rebuild
        rebuild(write=True)
        return
    output=[]; sources=[]
    # Horizons physical characteristics included in each raw response. Spherical reference radii.
    for ident,command,name,radius in [('sun',10,'太陽',695700000.0),('earth',399,'地球',6371008.4),
                                     ('moon',301,'月',1737400.0),('saturn',699,'土星',58232000.0)]:
        path=RAW/f'horizons_{ident}.json'
        params={**PARAMS,'COMMAND':f"'{command}'"}
        # A fresh checkout reuses the committed, actually acquired reference snapshot.
        # This avoids silently changing ephemerides when Horizons updates its solution.
        committed=ROOT/'Data/provenance'/path.name
        if not path.exists() and committed.exists():
            RAW.mkdir(parents=True,exist_ok=True);shutil.copyfile(committed,path)
        if not path.exists():
            r=requests.get('https://ssd.jpl.nasa.gov/api/horizons.api',params=params,timeout=60)
            r.raise_for_status(); save_json(path,r.json())
        payload=json.loads(path.read_text(encoding='utf-8')); result=payload.get('result','')
        if payload.get('error') or '$$SOE' not in result: raise RuntimeError(payload)
        assert 'Ecliptic of J2000.0' in result and 'KM-S' in result and '00:00:00.0000' in result
        row=next(csv.reader(result.split('$$SOE')[1].split('$$EOE')[0].strip().splitlines()))
        assert '2026-Sep-06 00:00:00.0000' in row[1]
        body=dict(id=ident,nameJa=name,radiusMeters=radius,positionMeters=[float(x)*1000 for x in row[2:5]],
                  velocityMetersPerSecond=[float(x)*1000 for x in row[5:8]],basis='ecliptic-j2000',epoch=EPOCH,
                  horizonsId=str(command),radiusConvention='mean spherical reference')
        if ident=='saturn': body.update(equatorialRadiusMeters=60268000.0,polarRadiusMeters=54364000.0)
        output.append(body)
        destination=ROOT/'Data/provenance'/path.name; destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_bytes(path.read_bytes())
        url=requests.Request('GET','https://ssd.jpl.nasa.gov/api/horizons.api',params=params).prepare().url
        sources.append(dict(path=destination.relative_to(ROOT).as_posix(),sha256=digest(path),requestParameters=params,
                            sourceUrl=url,observationDate=EPOCH,sourceUnits='KM-S; converted to meters and meters/second'))
    save_json(ROOT/'Content/Star/Data/bodies.json',dict(schemaVersion=1,epoch=EPOCH,timeScale='UTC (Horizons UT)',
              basis='ecliptic-j2000',origin='solar-system-barycenter',vectors='geometric, no light-time or aberration',
              bodies=output,provenance=sources))
    print('Horizons:',[(x['id'],x['positionMeters']) for x in output])

if __name__=='__main__': main()
