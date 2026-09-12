"""Create the small offline coverage index from ESA's published attribute URLs."""
from pathlib import Path
import re,json,hashlib,datetime,urllib.request
ROOT=Path(__file__).resolve().parents[2]
URL='https://esa-worldcover.s3.eu-central-1.amazonaws.com/esa_worldcover_grid_composites.fgb'
raw_path=ROOT/'work/earth-coverage/esa_worldcover_grid_composites.fgb'
raw_path.parent.mkdir(parents=True,exist_ok=True)
if not raw_path.exists():
    with urllib.request.urlopen(URL,timeout=60) as r:raw=r.read(64*1024*1024+1)
    if len(raw)>64*1024*1024:raise RuntimeError('Unexpectedly large coverage metadata')
    raw_path.write_bytes(raw)
raw=raw_path.read_bytes()
assert raw[:3]==b'fgb'
names=set(re.findall(rb's3://esa-worldcover-s2/rgbnir/2021/[NS][0-9]{2}/ESA_WorldCover_10m_2021_v200_([NS][0-9]{2}[EW][0-9]{3})_S2RGBNIR.tif',raw))
assert len(names)>19000
grid=bytearray(180*360)
for name in names:
    s=name.decode();lat=int(s[1:3])*(-1 if s[0]=='S' else 1);lon=int(s[4:])*(-1 if s[3]=='W' else 1)
    assert -90<=lat<90 and -180<=lon<180
    grid[(lat+90)*360+lon+180]=1
assert not grid[(11+90)*360-133+180] and grid[(35+90)*360+139+180]
out=ROOT/'Content/Star/Data/earth-rgbnir-coverage.bin';out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(grid)
meta={'source':URL,'retrievedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'observationYear':2021,
      'sourceSha256':hashlib.sha256(raw).hexdigest(),'resultSha256':hashlib.sha256(grid).hexdigest(),
      'format':'180 south-to-north rows x 360 west-to-east columns; uint8; SW integer degree cell; latitude -90..89 longitude -180..179',
      'tiles':len(names),'processing':'Extract exact RGBNIR 2021 object identifiers from FlatGeobuf attribute strings; provider coverage, not land/water classification',
      'license':'CC BY 4.0','credit':'ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium'}
(ROOT/'Data/earth-rgbnir-coverage.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
print(f'Coverage: {len(names)} tiles, {len(grid)} bytes, {meta["resultSha256"]}')
