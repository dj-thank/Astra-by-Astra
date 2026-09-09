"""Numeric reproduction of read-only chart/tangent formula; not engine execution."""
import json,math
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[3]
def unit(x): return x/np.linalg.norm(x)
reflect=np.array([1.,-1.,1.])
results=[]
for lat,lon in [(20.19,30.77),(0,0),(89.9,42),(-89.9,-73)]:
 la,lo=np.radians([lat,lon]); radial=np.array([np.cos(la)*np.cos(lo),np.cos(la)*np.sin(lo),np.sin(la)])
 ref=np.array([1.,0,0]) if abs(radial[2])>.99 else np.array([0.,0,1])
 east=unit(np.cross(ref,radial)); north=unit(np.cross(radial,east)); radius=1737400.
 for x,y in [(0.,0.),(1000.,-2000.),(30000.,20000.)]:
  def position(u,v):
   # Synthetic sloped height tests tangent signs only; never imported as terrain.
   return unit(radial*radius+east*u+north*v)*(radius+.07*u-.11*v)
  p=position(x,y); du=position(x+5,y)-position(x-5,y); dv=position(x,y+5)-position(x,y-5)
  n=unit(np.cross(du,dv)); t=unit(du)
  nue,tue=n*reflect,t*reflect
  bue=-unit(np.cross(nue,tue)) # bFlipTangentY=true
  vue=dv*reflect; vorth=unit(vue-tue*np.dot(vue,tue))
  assert np.dot(bue,vorth)>1-1e-10
  assert np.dot(tue,unit(du*reflect))>1-1e-10
  # Project gives UV1 +U/+V, including off-axis positions.
  d=unit(p); uv=radius*np.array([d@east,d@north])/(d@radial)
  assert np.allclose(uv,[x,y],atol=1e-7)
  results.append({'latitude':lat,'longitude':lon,'chart_xy_m':[x,y],'dot_bitangent_projected_plus_v':float(bue@vorth),'wrong_flip_dot':float(-bue@vorth)})
print(json.dumps({'status':'LOCAL_FORMULA_PASS','sample_count':len(results),'source':'LunarTerrainGeometry.cpp Chart/BuildPatch; StarLunarTerrainComponent.cpp Direction/FProcMeshTangent true','results':results},indent=2))
