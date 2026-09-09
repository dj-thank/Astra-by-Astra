"""Reproduce source-backed lunar panorama candidates without changing runtime materials."""
from pathlib import Path
import argparse,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[3]
def main():
 p=argparse.ArgumentParser();p.add_argument('--download',action='store_true');p.add_argument('--projection',action='store_true');p.add_argument('--full-stitch',action='store_true');args=p.parse_args()
 os.chdir(ROOT);Path('work/lunar-panorama').mkdir(parents=True,exist_ok=True)
 jobs=[]
 if args.download:jobs+=['download.py']
 if args.projection:jobs+=['fit_sequence.py','register_horizon.py','refine_horizon.py','project_terrain.py']
 if args.full_stitch:jobs+=['fit_full_sequence.py','stitch_full_sequence.py']
 if not jobs:p.error('Choose --download, --projection or --full-stitch. Preview uses separate tracked Blender CPU process.')
 for job in jobs:subprocess.run([sys.executable,str(Path(__file__).with_name(job))],check=True,env={**os.environ,'PYTHONUTF8':'1'})
if __name__=='__main__':main()
