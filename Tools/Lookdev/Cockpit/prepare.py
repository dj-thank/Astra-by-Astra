"""Deterministic photo-map crop/conversion. No generated noise, no Blender access."""
from pathlib import Path
import hashlib, json
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Content/Star/Art/Lookdev/Cockpit'
CROP = (64, 64, 320, 320)  # inside first tile; 0.25m at 2048 pixels / 2m

def periodic(a, width=16):
    # Blend opposing edges to their average, retaining the interior photo samples.
    a = a.copy()
    for axis in (0, 1):
        a = np.swapaxes(a, 0, axis)
        for i in range(width):
            t = .5 * (1 - i / width)
            lo, hi = a[i].copy(), a[-1-i].copy()
            a[i] = lo * (1-t) + hi*t
            a[-1-i] = hi * (1-t) + lo*t
        a = np.swapaxes(a, 0, axis)
    return a

def read(name, mode):
    im = Image.open(OUT/name)
    if mode == 'L' and im.mode in ('I', 'I;16', 'I;16B', 'I;16L'):
        return np.asarray(im.crop(CROP), dtype=np.float32)/65535
    return np.asarray(im.convert(mode).crop(CROP), dtype=np.float32)/255

def save(name, a):
    Image.fromarray(np.rint(np.clip(a, 0, 1)*255).astype('uint8')).save(OUT/name)

def main():
    save('SoftTouch_BaseColor.png', periodic(read('Source_RubberTiles_Diffuse_2K.png','RGB')))
    r = read('Source_RubberTiles_Rough_2K.png','L')
    # Intentional clean soft-touch finish, not a claim of measured rubber BRDF.
    save('SoftTouch_Roughness.png', periodic(np.clip(.64 + (r-r.mean())*.16,.58,.70)))
    n = read('Source_RubberTiles_NormalGL_2K.png','RGB')*2-1
    n[..., :2] *= .18
    n = periodic(n)
    n /= np.maximum(np.linalg.norm(n,axis=2,keepdims=True),1e-6)
    save('SoftTouch_NormalGL.png', n*.5+.5)
    n[..., 1] *= -1
    save('SoftTouch_NormalDX.png', n*.5+.5)
    records=[]
    for p in sorted(OUT.glob('*.png')):
        im=Image.open(p); im.verify()
        records.append(dict(file=p.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size,size=list(Image.open(p).size)))
    (OUT/'checksums.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'verified_images':len(records),'total_bytes':sum(x['bytes'] for x in records),'crop_pixels':CROP,'repeat_m':.25}))

if __name__ == '__main__': main()
