"""Predict UE5.8 legacy FBX's non-Nanite small-face removal, in centimeters."""
from pathlib import Path
import argparse, hashlib, importlib, json, sys, types
import numpy as np

root = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--blender-addons', type=Path, default=Path('C:/Program Files/Blender Foundation/Blender 5.1/5.1/scripts/addons_core'))
parser.add_argument('--source', type=Path, default=root / 'Content/Star/Art/CockpitV3')
parser.add_argument('--manifest', default='manifest_v04.json')
parser.add_argument('--output', default='ue_import_expectations_v04.json')
args = parser.parse_args()
package = types.ModuleType('io_scene_fbx')
package.__path__ = [str(args.blender_addons / 'io_scene_fbx')]
sys.modules['io_scene_fbx'] = package
reader = importlib.import_module('io_scene_fbx.parse_fbx')
source = args.source.resolve()
manifest = json.loads((source / args.manifest).read_text(encoding='utf-8'))
parts = manifest.get('model_parts')
if parts is None:
    validation = json.loads((source / manifest['validation']).read_text(encoding='utf-8'))
    parts = [{'name': manifest['part_name'], 'fbx': manifest['part_name']+'.fbx', 'triangles': validation['new']['triangles']}]
rows = {}
for part in parts:
    path = source / part['fbx']
    document, _ = reader.parse(str(path))
    arrays = {}
    def visit(element):
        if element.id in (b'Vertices', b'PolygonVertexIndex'):
            arrays[element.id] = np.asarray(element.props[0])
        for child in element.elems:
            visit(child)
    visit(document)
    indices = arrays[b'PolygonVertexIndex']
    indices = np.where(indices < 0, -indices-1, indices)
    meters = arrays[b'Vertices'].reshape(-1, 3)[indices].reshape(-1, 3, 3)
    assert len(meters) == part['triangles']
    cm = (meters * 100).astype(np.float32)
    normal = np.cross(cm[:, 1]-cm[:, 2], cm[:, 0]-cm[:, 2])
    # FbxStaticMeshImport.cpp GetTriangleAreaThreshold/GetSafeNormal: SMALL_NUMBER.
    removed = np.sum(normal*normal, axis=1) < 1e-8
    # StaticMeshBuilder.cpp also drops coincident corners at THRESH_POINTS_ARE_SAME.
    for a, b in ((0, 1), (1, 2), (2, 0)):
        removed |= (np.abs(cm[:, a]-cm[:, b]) <= 0.00002).all(axis=1)
    rows[part['name']] = {'sourceSha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                         'authoredTriangles': len(meters), 'removedSmallTriangles': int(removed.sum()),
                         'expectedRenderTriangles': int((~removed).sum())}
result = {'scope': 'UE5.8 legacy FBX, convert_scene_unit=true, non-Nanite, remove_degenerates=true; no arbitrary count tolerance',
          'sourceUnits': 'meters, transform baked, scale1', 'floatPositionUnits': 'centimeters',
          'squaredCrossThreshold': 1e-8, 'cornerDistanceThresholdCm': 0.00002, 'parts': rows}
(source / args.output).write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result, indent=2))
