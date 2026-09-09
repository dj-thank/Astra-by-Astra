"""Check one ordinary voyage and its continuous sunset; visual review remains explicit."""
from pathlib import Path
import argparse,json,math,re
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--packaged',action='store_true');p.add_argument('--night-lighting',action='store_true');args=p.parse_args()
d=args.capture;receipt=json.loads((d/'process.json').read_text(encoding='utf-8-sig'))
assert receipt.get('exitCode')==0
if args.packaged:assert receipt['packaged']
log=(d/'game.log').read_text(encoding='utf-8-sig',errors='replace')
assert 'LogExit: Exiting.' in log
assert not any(t in log for t in ['Fatal error:','Ran out of memory','Failed to compile Material'])
rows=[json.loads(line) for line in (d/'world-continuity.jsonl').read_text(encoding='utf-8').splitlines()]
assert len(rows)>100 and rows[-1]['elapsed']>100
assert all(r['normalStart'] for r in rows)
assert all(r['guidePreservedVoyage'] for r in rows if r['elapsed']>2)
assert all(not r['photoMode'] and not r['shipHidden'] and r['recoveries']==0 for r in rows)
assert all(r['cameraDistance']<300 for r in rows)
assert all(r['sunAimErrorDegrees']<5 for r in rows if r['elapsed']>20 and abs(r['sunClearance'])<5),'Sun is outside the horizon view'
for key in ['world','director','ship','map']:assert len({r[key] for r in rows})==1
assert abs(rows[0]['altitude']-450000)<1
assert all(r['rate'] in ((600,0) if args.night_lighting else (600,)) and 1e9<r['diskLuminance']<3e9 for r in rows)
if args.night_lighting:
 assert rows[-1]['rate']==0 and Image.open(d/'night-adapted.png').size==(3840,2160)
 assert all(math.isfinite(r[k]) for r in rows for k in ['environmentR','environmentG','environmentB'])
 assert max(r['environmentR']+r['environmentG']+r['environmentB'] for r in rows)>100
 assert min(r['minimumExposureEV'] for r in rows)<0
travel=0
for a,b in zip(rows,rows[1:]):
 assert b['utc']>=a['utc'] and b['simulationSeconds']>=a['simulationSeconds']
 delta=math.sqrt(sum((b[k]-a[k])**2 for k in ['localX','localY','localZ']))
 assert delta <= 310*(b['simulationSeconds']-a['simulationSeconds'])+0.02,'geographic discontinuity'
 travel+=delta
assert travel>4000
assert min(r['sunVisible'] for r in rows)<0.001 and max(r['sunVisible'] for r in rows)>0.999
partial=json.loads((d/'sun-on-horizon.png.json').read_text(encoding='utf-8'))
assert 0.2<partial['sunVisible']<0.8
uploads=re.findall(r'STAR observed Earth terrain committed: ([NS]\d+[EW]\d+)',log)
assert uploads,'No observed detail in the ordinary initial world'
for name in ['normal-start.png','sun-above.png','sun-on-horizon.png','sun-below.png']:
 assert Image.open(d/name).size==(3840,2160)
shot=json.loads((d/'sun-above.png.json').read_text(encoding='utf-8'))
cx,cy=int(shot['sunU']*3840),int(shot['sunV']*2160)
assert 64<cx<3776 and 64<cy<2096
pixels=Image.open(d/'sun-above.png').convert('RGB').crop((cx-64,cy-64,cx+64,cy+64))
bright=sum(1 for rgb in pixels.getdata() if min(rgb)>180)
assert bright>10,'Solar disk is not visibly bright at its projected position'
result={'state':'CONTINUOUS_WORLD_RUNTIME_CHECKS_PASS','packaged':receipt['packaged'],'normalInitialAltitudeMeters':rows[0]['altitude'],'travelMeters':travel,'oneWorldObject':rows[0]['world'],'oneDirectorObject':rows[0]['director'],'clockRate':600,'terrainUploads':uploads,'sunVisibleRange':[min(r['sunVisible'] for r in rows),max(r['sunVisible'] for r in rows)],'visualReviewRequired':True,'physicalInputAccepted':False}
(d/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
