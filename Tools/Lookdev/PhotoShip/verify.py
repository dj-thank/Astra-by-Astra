"""Verify downloaded photographic map provenance and deterministic image contracts."""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image
HERE=Path(__file__).parent
ROOT=HERE.resolve().parents[2]
def read(p):return json.loads(p.read_text(encoding='utf-8'))
records=read(HERE/'checksums.json')
for r in records:
    p=ROOT/r['file'];assert p.is_file(),p
    assert hashlib.sha256(p.read_bytes()).hexdigest()==r['sha256'],p
    im=Image.open(p);im.verify();assert list(Image.open(p).size)==r['size']
meta=read(HERE/'polyhaven_metadata.json')+read(HERE/'ceramic_metadata.json')
matched=[]
for slug,info,files in meta:
    if slug not in ['terlenka','rubber_tiles','interior_tiles']:continue
    assert info.get('authors'),slug
    for kind in ['Diffuse','Rough','nor_dx']:
        m=files[kind]['2k']['png'];p=ROOT/f'Content/Star/Art/PhotoShip/Sources/{slug}_{kind}.png'
        assert hashlib.md5(p.read_bytes()).hexdigest()==m['md5'],str(p)
        matched.append(p.name)
recipe=read(ROOT/'Data/photo_ship_bindings.json')
manifest=read(ROOT/'Art/Explorer/V2/art_manifest.json')
assert {b['material'] for b in recipe['material_bindings']}==set(manifest['materials'])
assert len(recipe['material_bindings'])==22
mapped=[b for b in recipe['material_bindings'] if b['status']=='PHOTO_MAP_CANDIDATE']
assert len(mapped)==14
assert all(b['parts'] for b in mapped)
assert sum(b['status']=='PHOTO_MAP_CANDIDATE_UNASSIGNED' for b in recipe['material_bindings'])==1
assert recipe['glass_invariant']==dict(base_color_linear=[0,0,0],transmission=.995,opacity=0,two_sided=False,neutral=True)
stats=[]
for s in recipe['map_sets']:
    r=np.asarray(Image.open(ROOT/s['maps']['roughness']),dtype=float)/255
    assert r.max()>r.min(),s['id']
    n=np.asarray(Image.open(ROOT/s['maps']['normal_dx']),dtype=float)/127.5-1
    lengths=np.linalg.norm(n,axis=2)
    assert np.max(np.abs(lengths-1))<.012,s['id']
    assert n[...,2].min()>0,s['id']
    assert np.max(np.abs(r[0]-r[-1]))<=1/255+.0001
    assert np.max(np.abs(r[:,0]-r[:,-1]))<=1/255+.0001
    stats.append(dict(id=s['id'],roughness_min=float(r.min()),roughness_max=float(r.max()),normal_max_length_error=float(np.max(abs(lengths-1)))))
for b in recipe['material_bindings']:
    assert b['parts']==[p['name'] for p in manifest['model_parts'] if b['material'] in p['materials']]
    if b['material'] in ['M_CanopyGlass','M_DisplayBlack','M_PhosphorIce','M_LabelWhite','M_NavigationWhite','M_NavigationRed','M_NavigationGreen']:
        assert b['status']=='PRESERVE_FUNCTIONAL'
for s in recipe['map_sets']:
    if s['id'] in ['CarbonShield','FoilResponse']:assert 'analytic flat' in s['normal_evidence']
assert len(recipe['map_sets'])==7
report=dict(status='LOCAL_IMAGE_DATA_PASS',image_hashes_verified=len(records),publisher_md5_verified=matched,material_coverage=22,assigned_opaque_photo_map_candidates=14,unassigned_foil_candidate=1,preserved_functional_families=7,unique_map_sets=7,stats=stats,runtime='NOT_RUN',independent_visual_review='ROOT_PENDING',baseline_sha256=hashlib.sha256((HERE/'baseline_cockpit.png').read_bytes()).hexdigest())
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
