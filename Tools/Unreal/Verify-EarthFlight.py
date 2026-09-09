"""Read back packaged Earth flight captures; do not infer physical-input acceptance."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'Tools/Unreal'))
from summarize_frames import summarize
p=argparse.ArgumentParser();p.add_argument('--runtime-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--package',default='Star-Win64-earth-flight');p.add_argument('--day-label',default='day-final-4k');p.add_argument('--night-label',default='night-final-4k');args=p.parse_args()
assert all(re.fullmatch(r'[A-Za-z0-9-]+',s) for s in [args.package,args.day_label,args.night_label])
root=args.runtime_root;release=root/'outputs'/args.package/'Windows';records={}
for label,altitude in [(args.day_label,35000),(args.night_label,160000)]:
 folder=root/'work/earth-flight'/label
 receipt=json.loads((folder/'process.json').read_text(encoding='utf-8-sig'))
 meta=json.loads((folder/'scene-0.json').read_text(encoding='utf-8-sig'))
 assert receipt.get('exitCode')==0 and receipt.get('packaged') and receipt.get('screenshotFresh'),label
 assert [meta['actualViewportWidth'],meta['actualViewportHeight']]==[3840,2160] and meta['screenPercentage']==100,label
 assert not meta['observationMode'] and meta['cameraMode']=='chase',label
 assert meta['flightDisplacementMeters']>5000 and meta['simulationTimeSeconds']>28 and meta['flightRecoveries']==0,label
 assert abs(meta['flightAltitudeMeters']-altitude)<100,label
 shot=folder/'scene-0.png';assert Image.open(shot).size==(3840,2160),label
 log=(folder/'game.log').read_text(encoding='utf-8-sig',errors='replace')
 assert not any(x in log for x in ['Fatal error:','Ran out of memory','Failed to compile Material']),label
 filenames=re.findall(r'Writing CSV to file\s*:\s*[^\r\n]*[/\\](Profile\([^\r\n]*?\)\.csv)',log)
 assert filenames,label
 frames=summarize(release/'Star/Saved/Profiling/CSV'/filenames[-1],5.0)
 records[label]={'viewport':[3840,2160],'screenPercentage':100,'flightDisplacementMeters':meta['flightDisplacementMeters'],'altitudeMeters':meta['flightAltitudeMeters'],'simulationTimeSeconds':meta['simulationTimeSeconds'],'recoveries':0,'cameraMode':'chase','imageSha256':hashlib.sha256(shot.read_bytes()).hexdigest(),'frameStatistics':frames}
def file_hash(path):
 with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
containers=[{'file':x.name,'sha256':file_hash(x)} for x in sorted((release/'Star/Content/Paks').iterdir()) if x.suffix in ('.pak','.utoc','.ucas')]
result={'state':'PACKAGED_FLIGHT_AND_4K_CAPTURE_PASS','scope':'Ordinary flight simulation with scripted controls, not physical joystick acceptance or all-route acceptance','executable':str(release/'Star/Binaries/Win64/Star.exe'),'executableSha256':file_hash(release/'Star/Binaries/Win64/Star.exe'),'containers':containers,'captures':records}
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
for name,r in records.items():print(name,round(r['frameStatistics']['afterWarmup']['averageFps'],2),'fps',round(r['flightDisplacementMeters']), 'm')
