"""Offline integrity and channel-contract verification; not a rendering test."""
import hashlib, json
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'Content/Star/Art/Lookdev/Cockpit'
HERE=Path(__file__).resolve().parent
def read_json(p): return json.loads(p.read_text(encoding='utf-8-sig'))

def main():
    records=read_json(OUT/'checksums.json')
    for r in records:
        p=ROOT/r['file']
        assert hashlib.sha256(p.read_bytes()).hexdigest()==r['sha256'],p
        with Image.open(p) as im:
            assert list(im.size)==r['size']; im.verify()
    receipt=read_json(HERE/'source_receipt.json')
    for k, suffix in [('Diffuse','Diffuse'),('Rough','Rough'),('NormalGL','NormalGL')]:
        p=OUT/f'Source_RubberTiles_{suffix}_2K.png'
        assert hashlib.md5(p.read_bytes()).hexdigest()==receipt['selected_files'][k]['md5']
    gl=np.asarray(Image.open(OUT/'SoftTouch_NormalGL.png')).astype(int)
    dx=np.asarray(Image.open(OUT/'SoftTouch_NormalDX.png')).astype(int)
    assert np.array_equal(gl[...,0],dx[...,0]) and np.array_equal(gl[...,2],dx[...,2])
    assert np.max(abs(gl[...,1]+dx[...,1]-255))<=1
    rough=np.asarray(Image.open(OUT/'SoftTouch_Roughness.png'))/255
    assert rough.min()>=.58-1/255 and rough.max()<=.70+1/255
    assert rough.max()>rough.min(), 'roughness lost source variation'
    for name in ['BaseColor','Roughness','NormalGL','NormalDX']:
        a=np.asarray(Image.open(OUT/f'SoftTouch_{name}.png')).astype(int)
        assert np.max(abs(a[0]-a[-1]))==0,name
        assert np.max(abs(a[:,0]-a[:,-1]))==0,name
    recipe=read_json(HERE/'recipe.json')
    manifest=read_json(ROOT/'Art/Explorer/V2/art_manifest.json')
    assert set(recipe['materials'])<=set(manifest['materials'])
    result={'status':'LOCAL_ASSET_CHECK_PASS','png_count':len(records),'bytes':sum(r['bytes'] for r in records),
            'source_md5_matches':3,'roughness_min_max':[float(rough.min()),float(rough.max())],
            'normal_green_flip':'pass','tile_boundary':'identical opposing edge pixels',
            'material_ids':'all exist in original V2 manifest','packaged_render':'NOT_RUN',
            'metal_photo_texture':'NOT_DELIVERED_SOURCE_NOT_VERIFIED'}
    (HERE/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__': main()
