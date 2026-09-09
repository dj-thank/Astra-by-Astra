"""Verify the exported real-DEM A/B bytes, including actual basal contact."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
args = parser.parse_args()
before = json.loads((args.directory/'v03.json').read_text(encoding='utf-8'))
after = json.loads((args.directory/'v04.json').read_text(encoding='utf-8'))
assert before['ground'] == after['ground'], 'Measured geometry changed'
assert before['clastCount'] == after['clastCount'], 'Clast population changed'
terrain = np.array(after['ground']['positions'])
triangles = terrain[np.array(after['ground']['indices']).reshape(-1, 3)]
positions = np.array(after['clasts']['positions'])
basal = []
for clast in after['ranges']:
    sides = (clast['count']-1)//4 if clast['count']>9 else 4
    basal.extend(positions[clast['first']:clast['first']+sides])
clearances = []
edge_unchecked = 0
for point in basal:
    candidates = triangles[np.all(point[:2] >= triangles[:, :, :2].min(axis=1)-1e-9, axis=1) &
                           np.all(point[:2] <= triangles[:, :, :2].max(axis=1)+1e-9, axis=1)]
    found = False
    for tri in candidates:
        a, b, c = tri
        matrix = np.column_stack(((b-a)[:2], (c-a)[:2]))
        u, v = np.linalg.solve(matrix, (point-a)[:2])
        if u >= -1e-8 and v >= -1e-8 and u+v <= 1+1e-8:
            clearances.append(float(point[2]-(a+(b-a)*u+(c-a)*v)[2]))
            found = True
            break
    if not found:
        edge_unchecked += 1
assert clearances and max(clearances) < 0, 'Floating basal vertex above actual measured triangle'
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
receipt = dict(status='CPU_REAL_DEM_GEOMETRY_PASS_NOT_UE', clasts=after['clastCount'],
               groundExactlyEqual=True, checkedBasalVertices=len(clearances),
               tileEdgeVerticesOutsideExport=edge_unchecked,
               basalClearanceMeters=[min(clearances), max(clearances)],
               beforeVertices=len(before['clasts']['positions']), afterVertices=len(after['clasts']['positions']),
               beforeTriangles=len(before['clasts']['indices'])//3, afterTriangles=len(after['clasts']['indices'])//3,
               sourceSha256={name:digest(args.directory/name) for name in ['v03.json','v04.json']},
               limitation='Only bases inside the exported tile are independently checked; native tests cover plane burial and streamed LOD.')
(args.directory/'geometry-receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps(receipt,indent=2))
