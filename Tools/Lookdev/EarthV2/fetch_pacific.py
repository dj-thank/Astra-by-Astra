"""Fetch one archived NASA GIBS region; preserve RGB and flag service gaps."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import io, json, hashlib, os
import requests, psutil
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt

ROOT = Path(__file__).resolve().parents[3]
CACHE = ROOT / 'work/earth-v2/pacific-raw'
OUT = ROOT / 'Content/Star/Art/EarthV2'
DATE = '2025-09-06'
BASE = 'https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi'
LAYER = 'MODIS_Aqua_CorrectedReflectance_TrueColor'


def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (CACHE / 'process.json').write_text(json.dumps({
        'pid': os.getpid(),
        'created': datetime.fromtimestamp(psutil.Process().create_time(), timezone.utc).isoformat(),
        'owner': 'earth-v2', 'purpose': 'NASA public imagery read',
        'deadline': (datetime.now(timezone.utc) + timedelta(minutes=30)).isoformat(),
        'stop': 'normal exit or exact PID',
    }), encoding='utf-8')
    canvas = Image.new('RGB', (8192, 8192))
    sources = []
    for y in range(4):
        for x in range(4):
            west, north = -160 + x * 4, 16 - y * 4
            params = dict(SERVICE='WMS', VERSION='1.1.1', REQUEST='GetMap',
                LAYERS=LAYER, STYLES='', SRS='EPSG:4326',
                BBOX=f'{west},{north-4},{west+4},{north}', WIDTH=2048, HEIGHT=2048,
                FORMAT='image/png', TRANSPARENT='TRUE', TIME=DATE)
            url = requests.Request('GET', BASE, params=params).prepare().url
            path = CACHE / f'aqua_{DATE}_{x}_{y}.png'
            if not path.exists():
                response = requests.get(url, timeout=90)
                response.raise_for_status()  # No retries on denial/service errors.
                image = Image.open(io.BytesIO(response.content))
                assert image.size == (2048, 2048)
                path.write_bytes(response.content)
            data = path.read_bytes()
            image = Image.open(path).convert('RGB')
            assert image.size == (2048, 2048)
            canvas.paste(image, (x*2048, y*2048))
            sources.append(dict(url=url, sha256=hashlib.sha256(data).hexdigest(),
                bbox=[west, north-4, west+4, north], bytes=len(data)))
            print(f'tile {x},{y} verified', flush=True)
    # Use the fully covered eastern half; retain the original bytes in the cache.
    canvas = canvas.crop((4096,0,8192,8192))
    rgb = np.asarray(canvas)
    valid = np.any(rgb != 0, axis=2)
    small = Image.fromarray(valid.astype(np.uint8)*255).resize((1024,2048), Image.Resampling.BOX)
    distance = distance_transform_edt(np.asarray(small) > 254)
    alpha = Image.fromarray(np.rint(np.clip((distance-3)/16,0,1)*255).astype(np.uint8))
    canvas.putalpha(alpha.resize((4096,8192),Image.Resampling.BILINEAR))
    path = OUT / 'T_Pacific_Aqua_20250906_8K.png'
    canvas.save(path, compress_level=4)
    assert np.array_equal(np.asarray(canvas)[:,:,:3], rgb)
    preview = canvas.copy()
    preview.thumbnail((1400,1400))
    preview.save(CACHE/'preview.png')
    metadata = dict(version=2, layer=LAYER, observationDate=DATE,
        retrievedAt=datetime.now(timezone.utc).isoformat(),
        retrievedBounds=dict(west=-160,south=0,east=-144,north=16),
        bounds=dict(west=-152,south=0,east=-144,north=16), size=[4096,8192],
        cropPixels=[4096,0,8192,8192],
        colorSpace='sRGB display RGB', datum='EPSG:4326, north-up pixel areas; rendered on mean-radius Earth',
        sourceResolutionMetres='MODIS 250/500 m bands; output sampling about 217 m does not increase observed resolution',
        originalRGBPreserved=True, alpha='Service gaps only, not a measured cloud mask',
        missingFraction=float(1-valid.mean()), texture=str(path.relative_to(ROOT)),
        sha256=hashlib.sha256(path.read_bytes()).hexdigest(), sources=sources,
        credit='NASA/GSFC, MODIS Aqua, LANCE, GIBS',
        limitations=['Archived composite, not current weather or calibrated albedo.',
            'Includes baked clouds, shading, atmosphere and ocean glint.',
            'No measured cloud height/volume retrieval or cross-date gap filling.'])
    (ROOT/'Data/earth_v2_pacific.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    print('PACIFIC VERIFIED',metadata['missingFraction'],flush=True)


if __name__ == '__main__':
    main()
