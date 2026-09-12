"""Encode actual UE viewport captures, retaining observed sample timestamps."""
import argparse,json,subprocess,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
rows=[json.loads(l) for l in (a.capture/'solar-flight.jsonl').read_text(encoding='utf-8-sig').splitlines()]
rows=[r for r in rows if (a.capture/r['file']).exists()];assert len(rows)>100
concat=a.capture/'movie.ffconcat';s='ffconcat version 1.0\n'
for i,r in enumerate(rows):
    s+=f"file '{r['file']}'\n"
    if i+1<len(rows):s+=f"duration {max(.001,rows[i+1]['t']-r['t']):.6f}\n"
concat.write_text(s,encoding='utf-8')
a.output.parent.mkdir(parents=True,exist_ok=True)
subprocess.run([shutil.which('ffmpeg'),'-hide_banner','-loglevel','error','-n','-f','concat','-safe','0','-i',str(concat),'-vf','fps=30','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(a.output)],check=True)
report={'duration':rows[-1]['t']-rows[0]['t'],'frames':len(rows),'sampleFps':len(rows)/(rows[-1]['t']-rows[0]['t']),'maximumSunDegrees':max(r['angle'] for r in rows),'minimumSunDegrees':min(r['angle'] for r in rows),'recoveries':max(r['recoveries'] for r in rows),'shipVisible':all(not r['shipHidden'] for r in rows),'simulationDelta':rows[-1]['simulationTime']-rows[0]['simulationTime'],'note':'Actual viewport samples; 30fps encoded container does not prove 30fps rendered frames or physical input'}
assert report['duration']>=30 and report['recoveries']==0 and report['shipVisible'] and report['simulationDelta']>30
a.output.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(report)
