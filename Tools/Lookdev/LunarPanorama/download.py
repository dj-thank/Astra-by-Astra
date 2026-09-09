# Run from repository root; intermediate output belongs to work/lunar-panorama.
import concurrent.futures,urllib.request,pathlib
out=pathlib.Path('Content/Star/Art/LunarPanorama/Source');out.mkdir(parents=True,exist_ok=True)
def get(n):
 p=out/f'AS17-147-{n}HR.jpg'
 if not p.exists():
  temporary=p.with_suffix('.part');urllib.request.urlretrieve('https://apollojournals.org/alsj/a17/'+p.name,temporary);temporary.replace(p)
 return p.name,p.stat().st_size
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
 for r in ex.map(get,range(22493,22520)): print(r,flush=True)
