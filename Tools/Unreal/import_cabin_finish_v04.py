"""Root-owned import of the bounded pedal supports and floor bedding."""
from pathlib import Path
import hashlib,json,sys
import unreal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bootstrap
bootstrap.require_dedicated_editor(ROOT)
source=ROOT/'Content/Star/Art/CabinFinishV4'
manifest=json.loads((source/'manifest.json').read_text(encoding='utf-8'))
fbx=source/manifest['fbx']
sha=hashlib.sha256(fbx.read_bytes()).hexdigest()
if sha!=manifest['sha256']:raise RuntimeError('Cabin support source changed')
materials=manifest['materials_reuse']
for path in materials.values():
    if not isinstance(unreal.load_asset(path),unreal.MaterialInterface):raise RuntimeError('Existing cabin finish missing: '+path)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
package='/Game/Star/Art/CabinFinishV4/SM_CabinFinishV4Support'
task=dict(kind='static_mesh',source=str(fbx),sha256=sha,package=package,object=package+'.SM_CabinFinishV4Support',
    settings=dict(part=dict(materials=list(materials)),nanite=False),provenance=dict(source=str(fbx.relative_to(ROOT)),sha256=sha))
asset,action=bootstrap.import_asset(task)
if asset.get_num_triangles(0)!=manifest['triangles']:raise RuntimeError('Unexpected imported support topology: '+str(asset.get_num_triangles(0)))
bootstrap.configure_asset(task,asset,materials)
bounds=asset.get_bounds()
for i,axis in enumerate(('x','y','z')):
    for edge,value in (('min',getattr(bounds.origin,axis)-getattr(bounds.box_extent,axis)),('max',getattr(bounds.origin,axis)+getattr(bounds.box_extent,axis))):
        if abs(value-manifest['bounds_m'][edge][i]*100)>.1:raise RuntimeError('Cabin support bounds mismatch')
bootstrap.save(asset)
path=ROOT/'Content/Star/Data/runtime_assets.json'
runtime=json.loads(path.read_text(encoding='utf-8'))
name='SM_CockpitCabinSupport'
existing=[p for p in runtime['shipParts'] if p['name']==name]
if len(existing)>1:raise RuntimeError('Duplicate cabin support binding')
part=dict(name=name,asset=asset.get_path_name(),role='cockpit',locationCm=[0,0,0])
if existing:existing[0].update(part)
else:runtime['shipParts'].append(part)
bootstrap.write_json(path,runtime)
bootstrap.write_json(ROOT/'work/v04/cabin-finish-import.json',dict(status='IMPORTED_AND_BOUND',sourceSha256=sha,triangles=asset.get_num_triangles(0),asset=asset.get_path_name(),action=action))
