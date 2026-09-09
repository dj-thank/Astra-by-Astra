"""Verify actual packaged, continuously piloted scenery, not a camera animation."""
from pathlib import Path
import argparse,csv,hashlib,json,math,re,sys
import numpy as np
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parent))
from summarize_frames import summarize,metrics
p=argparse.ArgumentParser()
p.add_argument('--runtime-root',type=Path,required=True);p.add_argument('--package',required=True)
p.add_argument('--capture',action='append',default=[],help='label:scene');p.add_argument('--controls')
p.add_argument('--journey',action='append',default=[],help='label:scene')
p.add_argument('--width',type=int,default=3840);p.add_argument('--height',type=int,default=2160)
p.add_argument('--output',type=Path,required=True);p.add_argument('--bind-release',action='store_true')
a=p.parse_args();root=a.runtime_root.resolve()
assert re.fullmatch(r'[A-Za-z0-9-]+',a.package)
release=root/'outputs'/a.package/'Windows';records={}
bodies=json.loads((root/'Content/Star/Data/bodies.json').read_text(encoding='utf-8'))['bodies']
earth=next(x for x in bodies if x['id']=='earth');sun=next(x for x in bodies if x['id']=='sun')
earth_center=np.array(earth['positionMeters'],float);sun_center=np.array(sun['positionMeters'],float);R=earth['radiusMeters']
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def lines(path):return [json.loads(x) for x in path.read_text(encoding='utf-8-sig').splitlines() if x.strip()]
def trajectory(folder,controls=False):
 t=lines(folder/'flight-trajectory.jsonl');assert len(t)>70
 wall=np.array([x['wallSeconds'] for x in t]);dt=np.diff(wall);assert np.all(dt>0),'Nonmonotonic trace including stale or terminal frames'
 pos=np.array([x['position'] for x in t]);cam=np.array([x['camera'] for x in t]);fwd=np.array([x['forward'] for x in t]);speed=np.array([x['speedMps'] for x in t])
 assert np.isfinite(pos).all() and np.isfinite(cam).all() and np.isfinite(fwd).all()
 assert not any(x['photoMode'] or x['observationMode'] or x['recoveries'] for x in t)
 assert np.max(np.linalg.norm(cam-pos,axis=1))<250,'Camera left the actual ship neighborhood'
 movement=np.linalg.norm(np.diff(pos,axis=0),axis=1)
 ordinary=np.ones(len(dt),bool)
 if controls:ordinary&=~((wall[1:]>27.7)&(wall[1:]<28.4)) # explicitly tested load of saved position
 assert np.all(movement[ordinary]<=np.maximum(speed[:-1],speed[1:])[ordinary]*dt[ordinary]+3),'Unexplained position jump'
 offset=pos-earth_center;radius=np.linalg.norm(offset,axis=1);to_sun=sun_center-pos
 mu=np.sum(offset/radius[:,None]*to_sun/np.linalg.norm(to_sun,axis=1)[:,None],axis=1)
 clearance=np.degrees(np.arcsin(np.clip(mu,-1,1))+np.arccos(np.clip(R/radius,0,1)))
 delta=np.max(np.abs(clearance-np.array([x['sunClearanceDegrees'] for x in t])))
 assert delta<1e-5,'Runtime Sun/horizon geometry disagrees with independent calculation'
 result={'samples':len(t),'engineElapsedSeconds':float(wall[-1]-wall[0]),'timingNote':'Legacy trace field wallSeconds is accumulated engine tick time, not an independent wall clock.','travelledMeters':float(movement[ordinary].sum()),'maxCameraDistanceMeters':float(np.max(np.linalg.norm(cam-pos,axis=1))),'maxSolarGeometryErrorDegrees':float(delta),'sunClearanceStartDegrees':float(clearance[0]),'sunClearanceEndDegrees':float(clearance[-1])}
 if controls:
  use=(wall>=8)&(wall<14);q=fwd[use];up=offset[use]/radius[use,None]
  # Ship translates nearly tangentially. Its actual path defines the local
  # heading independently of the camera controller's private yaw value.
  velocity=np.gradient(pos,wall,axis=0)[use];forward=velocity-up*np.sum(velocity*up,axis=1)[:,None];forward/=np.linalg.norm(forward,axis=1)[:,None]
  right=np.cross(up,forward)
  yaw=np.unwrap(np.arctan2(np.sum(q*right,axis=1),np.sum(q*forward,axis=1)))
  sweep=abs(math.degrees(yaw[-1]-yaw[0]));assert sweep>325,'Chase camera cannot complete an unrestricted look around'
  assert np.max(np.abs(np.diff(yaw)))<math.radians(30),'Yaw wrap caused a camera jump'
  result['freeLookSweepDegrees']=sweep
 return result
for pair in a.capture:
 label,scene=pair.split(':',1);assert re.fullmatch(r'[a-z0-9-]+',label)
 folder=root/'work/earth-flight'/label;m=read(folder/'scene-0.json');r=read(folder/'process.json')
 assert r.get('exitCode')==0 and r.get('packaged') and r.get('screenshotFresh')
 assert Image.open(folder/'scene-0.png').size==(a.width,a.height)
 assert [m['actualViewportWidth'],m['actualViewportHeight']]==[a.width,a.height] and m['screenPercentage']==100
 assert not(m['observationMode'] or m['photoMode'] or m['shipHidden']) and m['cameraMode']=='chase'
 assert m['flightDisplacementMeters']>5000 and m['simulationTimeSeconds']>28 and m['flightRecoveries']==0
 allowed={'sunrise':[120000],'sunset':[35000],'flight':[35000],'nightflight':[35000,160000],'orbit':[3*R]}[scene]
 assert min(abs(m['flightAltitudeMeters']-v) for v in allowed)<100
 if scene=='sunrise':assert m['sunHorizonClearanceDegrees']>m['initialSunHorizonClearanceDegrees']+.02
 if scene=='sunset':assert m['sunHorizonClearanceDegrees']<m['initialSunHorizonClearanceDegrees']-.02
 log=(folder/'game.log').read_text(encoding='utf-8-sig',errors='replace')
 assert not any(x in log for x in ['Fatal error:','Ran out of memory','Failed to compile Material'])
 names=re.findall(r'Writing CSV to file\s*:\s*[^\r\n]*[/\\](Profile\([^\r\n]*?\)\.csv)',log);assert names
 frame=summarize(release/'Star/Saved/Profiling/CSV'/names[-1],5.0)
 # A fixed interval before the known screenshot/readback at engine second 32.
 # Keep every frame in this interval, including any traversal or steering hitch.
 elapsed=0.0;steady=[]
 with (release/'Star/Saved/Profiling/CSV'/names[-1]).open(encoding='utf-8-sig',newline='') as stream:
  rows=csv.reader(stream);header=next(rows);column=header.index('FrameTime')
  for row in rows:
   try:value=float(row[column])
   except (IndexError,ValueError):continue
   if not math.isfinite(value) or value<=0:continue
   elapsed+=value/1000
   if 5<=elapsed<=30:steady.append(value)
 frame['fixedPreCaptureWindow']={'startSeconds':5,'endSeconds':30,'reason':'Normal flight before explicit screenshot/export; allFrames still includes the capture stall.','metrics':metrics(steady)}
 records[label]={'scene':scene,'nativeAtmosphere':r.get('nativeAtmosphere',False),'frameStatistics':frame,'trajectory':trajectory(folder),'imageSha256':hashlib.sha256((folder/'scene-0.png').read_bytes()).hexdigest(),'metadata':m}
if a.controls:
 folder=root/'work/earth-flight'/a.controls;r=read(folder/'process.json');assert r['exitCode']==0 and r['screenshotFresh']
 events=lines(folder/'controls-events.jsonl')
 required={'active-voyage-preset-does-not-relocate','pause','pause-freezes-simulation','resume','cockpit-during-flight','chase-during-flight','isolated-save','isolated-reload-position'}
 assert {e['event'] for e in events}==required and all(e['pass'] for e in events)
 assert (folder/'scenic-qa-save.json').is_file()
 records[a.controls]={'events':events,'trajectory':trajectory(folder,True)}
for pair in a.journey:
 label,scene=pair.split(':',1);assert re.fullmatch(r'[a-z0-9-]+',label)
 folder=root/'work/earth-flight'/label;r=read(folder/'process.json');m=read(folder/'scene-0.json')
 assert r['exitCode']==0 and r['packaged'] and r['screenshotFresh']
 assert not(m['photoMode'] or m['observationMode'] or m['shipHidden'])
 trace=trajectory(folder);minimum=280 if scene in ['sunrise','sunset'] else 80
 assert trace['engineElapsedSeconds']>=minimum and trace['travelledMeters']>18000
 if scene=='sunrise':assert trace['sunClearanceEndDegrees']>.25
 if scene=='sunset':assert trace['sunClearanceEndDegrees']<-.27
 pictures=lines(folder/'journey-frames.jsonl');assert len(pictures)>=2
 for pic in pictures:assert (folder/pic['file']).is_file()
 records[label]={'scene':scene,'trajectory':trace,'journeyImages':pictures,'metadata':m}
result={'state':'PACKAGED_PILOTED_SCENERY_FUNCTIONAL_PASS','scope':'Actual ordinary flight with scripted controls. Visual quality, physical input and universal absence of bugs are separate acceptance questions.','captures':records}
if a.bind_release:
 def digest(path):
  with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
 result['executable']={'path':str(release/'Star/Binaries/Win64/Star.exe'),'sha256':digest(release/'Star/Binaries/Win64/Star.exe')}
 result['containers']=[{'file':x.name,'sha256':digest(x)} for x in sorted((release/'Star/Content/Paks').iterdir()) if x.suffix in ['.pak','.utoc','.ucas']]
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
for label,r in records.items():print(label,round(r.get('frameStatistics',{}).get('afterWarmup',{}).get('averageFps',0),2),'fps',r['trajectory'])
