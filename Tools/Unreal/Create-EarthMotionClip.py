"""Encode sampled real viewport frames with recorded timing; never interpolate."""
from pathlib import Path
import argparse,datetime,hashlib,json,re,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
root=a.capture.resolve();receipt=json.loads((root/'process.json').read_text(encoding='utf-8-sig'))
assert receipt.get('exitCode')==0 and receipt.get('screenshotFresh')
started=datetime.datetime.fromisoformat(receipt['created'].replace('Z','+00:00')).timestamp()
frames=[json.loads(s) for s in (root/'motion-frames.jsonl').read_text(encoding='utf-8-sig').splitlines() if s.strip()]
assert len(frames)>=40
last=-1;manifest=[]
for item in frames:
 assert re.fullmatch(r'motion-\d{4}\.png',item['file'])
 file=root/item['file'];assert file.is_file() and file.stat().st_mtime>=started
 assert item['wallSeconds']>last;last=item['wallSeconds']
 manifest.append(dict(item,sha256=hashlib.sha256(file.read_bytes()).hexdigest()))
listing=['ffconcat version 1.0']
for i,item in enumerate(frames):
 listing.append("file '"+(root/item['file']).as_posix()+"'")
 duration=(frames[i+1]['wallSeconds']-item['wallSeconds']) if i+1<len(frames) else .125
 assert 0<duration<2
 listing.append(f'duration {duration:.6f}')
listing.append("file '"+(root/frames[-1]['file']).as_posix()+"'")
concat=root/'motion.ffconcat';concat.write_text('\n'.join(listing)+'\n',encoding='utf-8')
a.output.parent.mkdir(parents=True,exist_ok=True)
ffmpeg=shutil.which('ffmpeg');assert ffmpeg
subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-n','-f','concat','-safe','0','-i',str(concat),'-fps_mode','vfr','-c:v','libx264','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(a.output)],check=True)
summary={'source':'Actual packaged viewport PNG sequence','sampling':'Up to 8 fps; accumulated engine tick timing, no interpolated frames. The legacy field wallSeconds is not an independent wall clock. Not native-FPS or 4K temporal acceptance.','physicalInput':False,'frames':len(frames),'durationSeconds':frames[-1]['wallSeconds']-frames[0]['wallSeconds']+.125,'frameManifest':manifest,'videoSha256':hashlib.sha256(a.output.read_bytes()).hexdigest()}
a.output.with_suffix('.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(len(frames),'frames',round(summary['durationSeconds'],2),'seconds',a.output)
