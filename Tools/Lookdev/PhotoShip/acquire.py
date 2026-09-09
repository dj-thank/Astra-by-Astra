"""Download public photo sources, retain bytes and checksums. No UE mutation."""
import hashlib,json,re
from pathlib import Path
import requests
import io,zipfile
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'Content/Star/Art/PhotoShip'
SRC=OUT/'Sources'
SRC.mkdir(parents=True,exist_ok=True)
def get(url):
    r=requests.get(url,timeout=60);r.raise_for_status();return r
def save(name,url):
    b=(SRC/name).read_bytes() if (SRC/name).exists() else get(url).content
    (SRC/name).write_bytes(b)
    return dict(file=str((SRC/name).relative_to(ROOT)),url=url,sha256=hashlib.sha256(b).hexdigest(),bytes=len(b))
if __name__=='__main__':
    from concurrent.futures import ThreadPoolExecutor
    jobs=[('BrushedAluminium.jpg','https://upload.wikimedia.org/wikipedia/commons/1/13/Brushed_Aluminium.jpg'),('CassiniBlanket.jpeg','https://assets.science.nasa.gov/dynamicimage/assets/science/psd/solar/internal_resources/1493/Blanket.jpeg'),('CassiniHardware.jpg','https://assets.science.nasa.gov/dynamicimage/assets/science/psd/solar/2023/07/103_jpl-28369ec.jpg'),('ParkerHeatShield.jpg','https://www.nasa.gov/wp-content/uploads/2018/07/5d29486.jpg'),('ParkerHardware.jpg','https://www.nasa.gov/wp-content/uploads/2018/07/41983266465_760f51c5a0_o.jpg')]
    meta=json.loads((Path(__file__).parent/'polyhaven_metadata.json').read_text(encoding='utf-8'))+json.loads((Path(__file__).parent/'ceramic_metadata.json').read_text(encoding='utf-8'))
    for slug,info,files in meta:
        if slug not in ['terlenka','rubber_tiles','interior_tiles']:continue
        for kind in ['Diffuse','Rough','nor_dx']:
            choices=files[kind]['2k'];ext='png' if 'png' in choices else 'jpg'
            jobs.append((slug+'_'+kind+'.'+ext,choices[ext]['url']))
    for filename,metadata_name in [('OrionCockpit.jpg','cockpit_photo_metadata.json'),('ShuttleCockpit.jpg','display_photo_metadata.json'),('ShuttleFlightDeck.jpg','flightdeck_photo_metadata.json')]:
        data=json.loads((Path(__file__).parent/metadata_name).read_text(encoding='utf-8'))
        jobs.append((filename,data['collection']['items'][0]['links'][0]['href']))
    # Known 403 was already recorded; no alternate route or repeated request.
    jobs=[p for p in jobs if p[0]!='BrushedAluminium.jpg']
    receipts=[dict(file='BrushedAluminium.jpg',url='https://upload.wikimedia.org/wikipedia/commons/1/13/Brushed_Aluminium.jpg',error='403 observed 2026-09-06; acquisition stopped')]
    for name,url in jobs:
        try: receipts.append(save(name,url))
        except requests.HTTPError as e: receipts.append(dict(file=name,url=url,error=str(e)))
    for slug,metadata_name in [('Metal063','ambient_metadata.json'),('Plastic001','plastic_metadata.json')]:
        target=SRC/slug;target.mkdir(exist_ok=True)
        metadata=get('https://ambientcg.com/api/v2/full_json?id='+slug+'&include=downloadData').json()
        (Path(__file__).parent/metadata_name).write_text(json.dumps(metadata,indent=2),encoding='utf-8')
        url='https://ambientcg.com/get?file='+slug+'_1K-JPG.zip'
        if not (target/(slug+'_1K-JPG_Color.jpg')).exists():
            archive=zipfile.ZipFile(io.BytesIO(get(url).content))
            for name in archive.namelist():
                if name.endswith('.jpg'):(target/Path(name).name).write_bytes(archive.read(name))
        for p in sorted(target.glob('*.jpg')):
            receipts.append(dict(file=p.relative_to(ROOT).as_posix(),url=url,archive_member=p.name,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size))
    (Path(__file__).parent/'downloads.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
    print(json.dumps(receipts,indent=2))
