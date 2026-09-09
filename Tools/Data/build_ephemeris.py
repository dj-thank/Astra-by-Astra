"""Acquire dated Horizons vectors and NAIF orientations; build an offline UTC table.

No texture, weather or lighting values are altered by this tool. All downloads,
source epochs, frames and interpolation checks remain attributable.
"""
from pathlib import Path
import csv,datetime as dt,hashlib,json,struct,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'work/earth-time/python'))
import numpy as np,requests,spiceypy as spice
RAW=ROOT/'work/earth-time/sources';RAW.mkdir(parents=True,exist_ok=True)
OUT=ROOT/'Content/Star/Data';PROV=ROOT/'Data/ephemeris';PROV.mkdir(parents=True,exist_ok=True)
START=dt.datetime(2026,1,1,tzinfo=dt.timezone.utc);STOP=dt.datetime(2027,1,1,tzinfo=dt.timezone.utc)
STEP=3600;COUNT=int((STOP-START).total_seconds()/STEP)+1
BODIES=[('sun',10,'IAU_SUN'),('earth',399,'ITRF93'),('moon',301,'MOON_ME'),('saturn',699,'IAU_SATURN')]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def fetch(url,path,params=None):
 receipt=path.with_suffix(path.suffix+'.receipt.json')
 if path.exists():
  assert receipt.exists(),f'Unreceipted file retained: {path}'
  m=json.loads(receipt.read_text(encoding='utf-8'));assert digest(path)==m['sha256'];return m
 r=requests.get(url,params=params,timeout=(15,90));r.raise_for_status();path.write_bytes(r.content)
 m={'url':r.url,'acquiredUtc':dt.datetime.now(dt.timezone.utc).isoformat(),'sha256':digest(path),'bytes':path.stat().st_size}
 dump(receipt,m);return m
def vectors(ident,command,half=False):
 params=dict(format='json',COMMAND=f"'{command}'",CENTER="'500@0'",EPHEM_TYPE="'VECTORS'",TIME_TYPE="'UT'",
  START_TIME="'2026-01-01 00:30:00'" if half else "'2026-01-01 00:00:00'",STOP_TIME="'2027-01-01 00:00:00'",
  STEP_SIZE="'31 d'" if half else "'1 h'",REF_SYSTEM="'ICRF'",REF_PLANE="'ECLIPTIC'",OUT_UNITS="'KM-S'",VEC_TABLE="'2'",VEC_CORR="'NONE'",CSV_FORMAT="'YES'",OBJ_DATA="'YES'",TIME_DIGITS="'FRACSEC'")
 path=RAW/f'horizons-{ident}{"-holdout" if half else ""}.json'
 receipt=fetch('https://ssd.jpl.nasa.gov/api/horizons.api',path,params)
 payload=json.loads(path.read_text(encoding='utf-8'));assert not payload.get('error'),payload.get('error')
 text=payload['result'];assert 'Ecliptic of J2000.0' in text and 'KM-S' in text
 rows=list(csv.reader(text.split('$$SOE')[1].split('$$EOE')[0].strip().splitlines()))
 times=[];states=[]
 for row in rows:
  moment=dt.datetime.strptime(row[1].strip().removeprefix('A.D. '),'%Y-%b-%d %H:%M:%S.%f').replace(tzinfo=dt.timezone.utc)
  times.append(moment.timestamp());states.append([float(x)*1000 for x in row[2:8]])
 return np.array(times),np.array(states),receipt
def slerp(a,b,u):
 d=float(a@b)
 if d<0:b=-b;d=-d
 if d>.999999:q=a*(1-u)+b*u;return q/np.linalg.norm(q)
 angle=np.arccos(np.clip(d,-1,1));return (a*np.sin((1-u)*angle)+b*np.sin(u*angle))/np.sin(angle)
def hermite(a,b,u):
 return (2*u**3-3*u*u+1)*a[:3]+(u**3-2*u*u+u)*STEP*a[3:6]+(-2*u**3+3*u*u)*b[:3]+(u**3-u*u)*STEP*b[3:6]
def main():
 kernels=[]
 for name in ['naif0012.tls','pck00011.tpc','moon_pa_de421_1900-2050.bpc','moon_080317.tf']:
  path=ROOT/'Data/provenance/kernels'/name;spice.furnsh(str(path));kernels.append({'path':str(path.relative_to(ROOT)),'sha256':digest(path)})
 name='earth_1962_260806_2126_combined.bpc';path=RAW/name
 m=fetch('https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/'+name,path);spice.furnsh(str(path));kernels.append(m)
 data=np.zeros((COUNT,4,10),dtype='<f8');sources=[];checks=[]
 times=np.arange(COUNT)*STEP+START.timestamp()
 ets=np.array([spice.str2et(dt.datetime.fromtimestamp(t,dt.timezone.utc).isoformat().replace('+00:00','Z')) for t in times])
 for j,(ident,command,frame) in enumerate(BODIES):
  vt,vs,source=vectors(ident,command);assert np.array_equal(vt,times)
  data[:,j,:6]=vs;sources.append(dict(source,body=ident,frame='ECLIPJ2000',origin='solar-system barycenter',correction='NONE',sourceUnits='km and km/s; converted to SI'))
  for i,et in enumerate(ets):data[i,j,6:]=spice.m2q(spice.pxform(frame,'ECLIPJ2000',float(et)))
  ht,hv,hs=vectors(ident,command,True);sources.append(dict(hs,body=ident,role='independent half-hour position checks'))
  max_pos=0;max_angle=0;check_rows=[]
  for t,truth in zip(ht,hv):
   index=min(int((t-times[0])/STEP),COUNT-2);u=(t-times[index])/STEP
   a,b=data[index,j],data[index+1,j];pred=hermite(a,b,u);error=float(np.linalg.norm(pred-truth[:3]));max_pos=max(max_pos,error)
   iso=dt.datetime.fromtimestamp(t,dt.timezone.utc).isoformat().replace('+00:00','Z')
   qtruth=spice.m2q(spice.pxform(frame,'ECLIPJ2000',spice.str2et(iso)));q=slerp(a[6:],b[6:],u)
   angle=float(2*np.arctan2(np.linalg.norm((spice.qxq(qtruth*np.array([1,-1,-1,-1]),q))[1:]),abs(float(qtruth@q)))*180/np.pi);max_angle=max(max_angle,angle)
   check_rows.append({'utc':iso,'unix':float(t),'position':truth[:3].tolist(),'orientation':qtruth.tolist()})
  assert max_pos<20,(ident,max_pos)
  assert max_angle<0.0001,(ident,max_angle)
  checks.append({'body':ident,'maxPositionErrorMeters':max_pos,'maxOrientationErrorDegrees':max_angle,'samples':check_rows})
  print(ident,len(vt),'states; max holdout error',max_pos,'m',max_angle,'deg',flush=True)
 assert np.isfinite(data).all()
 path=OUT/'ephemeris-2026.bin'
 path.write_bytes(struct.pack('<8sIIdd',b'STAREPH1',COUNT,4,START.timestamp(),float(STEP))+data.tobytes())
 meta={'schemaVersion':1,'file':path.name,'sha256':digest(path),'startUtc':START.isoformat(),'endUtc':STOP.isoformat(),'timeScale':'UTC (Horizons UT), leap-second conversion by NAIF0012 for SPICE orientation','basis':'ECLIPJ2000 right handed; meters','origin':'solar-system barycenter','bodyOrder':[x[0] for x in BODIES],'bodyFixedFrames':{x[0]:x[2] for x in BODIES},'sampleSeconds':STEP,'count':COUNT,'interpolation':'cubic Hermite position using source velocity; shortest-arc quaternion SLERP','lightPropagation':'geometric, no light-time or aberration correction','earthOrientation':'ITRF93; source last datum 2026-08-06, prediction to 2026-11-02, extended prediction afterward. Historical error estimated <3 microradians; long-term predicted error may reach 5-6 milliradians. Interpolation residual is not absolute accuracy.','moonOrientation':'MOON_ME / DE421, retained mapping datum; Horizons positions use current solution.','kernelSources':kernels,'vectorSources':sources,'library':{'spiceypy':spice.__version__,'toolkit':spice.tkvrsn('TOOLKIT')},'checks':[{k:v for k,v in c.items() if k!='samples'} for c in checks]}
 dump(OUT/'ephemeris-2026.json',meta);dump(PROV/'ephemeris-checks.json',checks);dump(PROV/'provenance.json',meta)
 spice.kclear();print('Ephemeris built',path.stat().st_size,'bytes',flush=True)
if __name__=='__main__':main()
