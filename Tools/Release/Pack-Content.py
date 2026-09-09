"""Package redistributable project assets, excluding engine-derived VR material."""
from pathlib import Path
import argparse,hashlib,json,zipfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--tag',default='v0.4.0-preview.1');args=parser.parse_args()
(ROOT/'outputs').mkdir(parents=True,exist_ok=True)
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()
files=[]
for folder in ['Content','Art']:
 for p in sorted((ROOT/folder).rglob('*')):
  if not p.is_file():continue
  if p.name=='M_VRPanel.uasset' or p.suffix in {'.blend1','.blend2','.log','.pdb'}:continue
  if any(part in {'Saved','Intermediate','__pycache__'} for part in p.parts):continue
  files.append(p)
assert files
manifest=[{'path':p.relative_to(ROOT).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)} for p in files]
(ROOT/'Data/content-files.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
def art_source(p):
 rel=p.relative_to(ROOT)
 return rel.parts[0]=='Art' or (rel.parts[:3]==('Content','Star','Textures') and p.suffix!='.uasset')
archives=[]
for name,selection in [('STAR-Content.zip',[p for p in files if not art_source(p)]),('STAR-Art-Sources.zip',[p for p in files if art_source(p)])]:
 out=ROOT/'outputs'/name;assert not out.exists(),'Preserve existing release archives'
 with zipfile.ZipFile(out,'x',zipfile.ZIP_DEFLATED,compresslevel=3) as z:
  for p in selection:z.write(p,p.relative_to(ROOT).as_posix())
 with zipfile.ZipFile(out) as z:assert z.testzip() is None
 assert out.stat().st_size<2_000_000_000,'Release asset exceeds upload size budget'
 archives.append({'name':name,'url':f'https://github.com/dj-thank/STAR/releases/download/{args.tag}/{name}','sha256':sha(out),'bytes':out.stat().st_size,'files':len(selection)})
(ROOT/'Data/content-release.json').write_text(json.dumps({'tag':args.tag,'archives':archives,'notices':'THIRD_PARTY_NOTICES.md','engineDerivedMaterialExcluded':'Content/Star/Materials/M_VRPanel.uasset'},indent=2)+'\n',encoding='utf-8')
print(json.dumps(archives))
