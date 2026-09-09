"""Verify distribution bytes in Git survive Windows newline conversion (default: index)."""
import argparse
import hashlib
import json
import subprocess
from acquire import ROOT

def main():
    p=argparse.ArgumentParser();p.add_argument('--ref',default='',help='Empty=index; HEAD=committed tree');args=p.parse_args()
    manifest=json.loads((ROOT/'Data/manifest.json').read_text(encoding='utf-8'))
    bodies=json.loads((ROOT/'Content/Star/Data/bodies.json').read_text(encoding='utf-8'))
    entries=manifest['assets']+bodies['provenance']+bodies['orientationSources']
    for entry in entries:
        git=subprocess.Popen(['git','cat-file','blob',args.ref+':'+entry['path']],cwd=ROOT,stdout=subprocess.PIPE)
        h=hashlib.sha256()
        while chunk:=git.stdout.read(1024*1024):h.update(chunk)
        git.stdout.close();code=git.wait()
        if code or h.hexdigest()!=entry['sha256']:raise RuntimeError('Git byte mismatch: '+entry['path'])
    print('Verified',len(entries),'runtime/provenance blobs in',args.ref or 'Git index')

if __name__=='__main__':main()
