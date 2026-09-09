"""Verify captured runtime clock and local flight evidence without inventing device acceptance."""
from pathlib import Path
import argparse,json,hashlib,math,re
from PIL import Image
from summarize_frames import summarize

p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--require-packaged',action='store_true');p.add_argument('--time-checks',action='store_true');args=p.parse_args()
d=args.capture
def read(name):return json.loads((d/name).read_text(encoding='utf-8-sig'))
receipt=read('process.json');meta=read('scene-0.json');log=(d/'game.log').read_text(encoding='utf-8-sig',errors='replace')
assert 'LogExit: Exiting.' in log,'Runtime did not reach normal shutdown'
assert not any(s in log for s in ['Fatal error:','Ran out of memory','Failed to compile Material']), 'Runtime/render failure'
if args.require_packaged:assert receipt['packaged'] and receipt.get('exitCode')==0,'Packaged process success required'
assert meta['actualViewportWidth']==3840 and meta['actualViewportHeight']==2160 and meta['screenPercentage']==100
assert meta['flightRecoveries']==0 and not meta['observationMode'] and not meta['photoMode'] and not meta['shipHidden']
assert meta['flightDisplacementMeters']>4000 and meta['simulationTimeSeconds']>25
assert not meta['syntheticCloudDetail'] and not meta['inventedWeatherAdvection']
assert Image.open(d/'scene-0.png').size==(3840,2160)
trace=[json.loads(s) for s in (d/'astronomy-trajectory.jsonl').read_text(encoding='utf-8').splitlines()]
assert len(trace)>80 and all(math.isfinite(v) for r in trace for v in [r['utc'],r['rate'],*r['earthLocal'],*r['earthCenter'],*r['sunCenter']])
events=[]
if args.time_checks:
 events=[json.loads(s) for s in (d/'time-events.jsonl').read_text(encoding='utf-8').splitlines()]
 expected={'fixed-clock-while-flying','date-change-preserves-geography','reject-unsupported-time','editable-utc-applied','sixty-times-clock','pause-freezes-utc','save-utc','reload-utc-position-rate'}
 assert {e['event'] for e in events}==expected and all(e['pass'] for e in events)
 assert (d/'time-six-hours.png').is_file() and (d/'time-eighteen-hours.png').is_file()
tiles=re.findall(r'STAR observed Earth terrain committed: ([NS]\d+[EW]\d+) source DEM=(\d+)x(\d+) imagery=(\d+)x(\d+) vertices=(\d+)',log)
assert tiles,'No actual streamed geometry upload observed'
profiles=re.findall(r'Writing CSV to file\s*:\s*([^\r\n]+\.csv)',log)
frames=summarize(Path(profiles[-1]),5) if profiles and Path(profiles[-1]).is_file() else None
result={'state':'RUNTIME_CLOCK_FLIGHT_CHECKS_PASS','packaged':receipt['packaged'],'processExitCodeObserved':receipt.get('exitCode'),'shutdownObservedInLog':True,'visualQualityAccepted':False,'visualQualityNote':'This verifier does not assess seams, masking, realism or headset output; visual review is separate.','physicalInputAccepted':False,'headsetAccepted':False,'viewport':[3840,2160],'renderScalePercent':100,'displacementMeters':meta['flightDisplacementMeters'],'recoveries':meta['flightRecoveries'],'utc':meta['worldUtc'],'clockEvents':events,'observedRasterUploads':[{'tile':t[0],'demPixels':[int(t[1]),int(t[2])],'imageryPixels':[int(t[3]),int(t[4])],'vertices':int(t[5])} for t in tiles],'frames':frames,'screenshotSha256':hashlib.sha256((d/'scene-0.png').read_bytes()).hexdigest()}
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(result['state'], 'packaged=',result['packaged'],'clock checks=',len(events),'visual acceptance separate')
