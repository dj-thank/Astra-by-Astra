"""Read actual package receipts and CSV GPU timings; no synthetic FPS claims."""
from pathlib import Path
import csv,json,statistics
ROOT=Path(__file__).resolve().parents[2];QA=ROOT/'work/solar-qa'
def profile(name):
    path=next((QA/name/'user/Saved/Profiling/CSV').glob('*.csv'))
    rows=[]
    with path.open(encoding='utf-8-sig') as f:
        for row in csv.DictReader(f):
            try:rows.append((float(row['GPUTime']),float(row['FrameTime'])))
            except (ValueError,KeyError,TypeError):pass
    # Identical fixed-step route: discard startup and the final screenshot.
    rows=rows[300:750];assert len(rows)>=300
    gpu=sorted(r[0] for r in rows);frame=sorted(r[1] for r in rows)
    return {'samples':len(rows),'gpuMedianMs':statistics.median(gpu),'gpuP95Ms':gpu[int(.95*(len(gpu)-1))],
            'frameMedianMs':statistics.median(frame),'frameP95Ms':frame[int(.95*(len(frame)-1))]}
current_name=next(n for n in ['gpu-delivery','gpu-final','gpu-current'] if (QA/n).exists())
current,legacy=profile(current_name),profile('gpu-legacy')
delta=current['gpuMedianMs']-legacy['gpuMedianMs']
report={'currentRun':current_name,'current':current,'legacy':legacy,'addedGpuMedianMs':delta,'within2msBudget':delta<=2,
        'frameP95Within30fps':current['frameP95Ms']<=33.5,'measurement':'Packaged Win64 DX12, actual CSV GPUTime, 3840x2160 requested and per-shot readback; paired stationary near-Sun scenes, no movie capture.'}
for name in [next(n for n in ['motion-delivery','motion-final','motion-current'] if (QA/n).exists()),'motion-legacy']:
    rows=[json.loads(l) for l in (QA/name/'solar-flight.jsonl').read_text(encoding='utf-8-sig').splitlines()]
    report[name]={'samples':len(rows),'first':rows[0],'last':rows[-1],'angleMin':min(r['angle'] for r in rows),'angleMax':max(r['angle'] for r in rows)}
earth_name='normal-earth-full' if (QA/'normal-earth-full').exists() else 'normal-earth'
earth=[json.loads(l) for l in (QA/earth_name/'world-continuity.jsonl').read_text(encoding='utf-8-sig').splitlines()]
report['earth']={'samples':len(earth),'normalStart':any(r['normalStart'] for r in earth),'visibilityMin':min(r['sunVisible'] for r in earth),'visibilityMax':max(r['sunVisible'] for r in earth),'worldCount':len(set(r['world'] for r in earth))}
(QA/'metrics.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
