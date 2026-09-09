"""Original deterministic ambient compositions. MIT; no samples or external services."""
from pathlib import Path
import hashlib,json,wave
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Content/Star/Audio/Source';OUT.mkdir(parents=True,exist_ok=True)
RATE=48000
SCORES={
 'BGM_EARTH_SUNRISE':[[48,55,62,67],[53,60,64,69],[50,57,60,65],[55,62,67,69]],
 'BGM_MOON_APPROACH':[[45,52,59,64],[48,55,62,67],[41,48,55,60],[43,50,57,62]],
 'BGM_MOON_SURFACE':[[38,45,52,57],[41,48,55,60],[36,43,50,55],[38,45,52,59]],
 'BGM_SATURN_RINGS':[[46,53,60,65],[49,56,63,68],[44,51,58,65],[46,53,60,67]],
 'BGM_DEEP_CRUISE':[[40,47,54,59],[43,50,57,62],[38,45,52,59],[40,47,54,61]],
}
for name,chords in SCORES.items():
 duration=64; audio=np.zeros((RATE*duration,2),dtype=np.float64)
 for j,chord in enumerate(chords):
  start=j*16;length=20 if j<3 else 16;t=np.arange(RATE*length)/RATE
  env=np.minimum(1,t/3)*np.minimum(1,(length-t)/5)
  for k,note in enumerate(chord):
   hz=440*2**((note-69)/12);pan=(k-1.5)/5
   for c in range(2):
    phase=2*np.pi*(hz*(1+(c*2-1)*0.0007))*t
    tone=np.sin(phase)+0.15*np.sin(2*phase+0.2)+0.04*np.sin(3*phase)
    modulation=0.88+0.12*np.sin(2*np.pi*(0.08+k*0.017)*t+k)
    audio[start*RATE:start*RATE+len(t),c]+=0.035*env*tone*modulation*(1+pan*(c*2-1))
 # Deterministic edge fade makes loop restarts click-free, with a short musical breath.
 t=np.arange(len(audio))/RATE;audio*=np.minimum(1,t/2)[:,None]*np.minimum(1,(duration-t)/2)[:,None]
 assert np.isfinite(audio).all() and np.max(np.abs(audio))<0.8
 with wave.open(str(OUT/(name+'.wav')),'wb') as f:
  f.setparams((2,2,RATE,0,'NONE','not compressed'));f.writeframes(np.round(audio*32767).astype('<i2').tobytes())
 print(name)
items=[]
for p in sorted(OUT.glob('*.wav')):
 with wave.open(str(p)) as f: duration=f.getnframes()/f.getframerate()
 items.append({'id':p.stem,'file':p.relative_to(ROOT).as_posix(),'seconds':duration,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'license':'MIT','source':'original deterministic synthesis'})
(OUT.parent/'audio-manifest.json').write_text(json.dumps({'sampleRateHz':RATE,'channels':2,'pcmBits':16,'items':items},indent=2)+'\n',encoding='utf-8')
