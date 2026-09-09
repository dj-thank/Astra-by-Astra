"""Dedicated import and binding of the validated local-pivot control replacement."""
from pathlib import Path
import hashlib, json, sys
import unreal

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bootstrap
bootstrap.require_dedicated_editor(ROOT)
SOURCE=ROOT/'Content/Star/Art/CockpitControlsV4'
manifest=json.loads((SOURCE/'manifest.json').read_text(encoding='utf-8'))
validation=json.loads((SOURCE/'validation.json').read_text(encoding='utf-8'))
expected=json.loads((SOURCE/'ue_import_expectations.json').read_text(encoding='utf-8'))['parts']['SM_ControlStick']
fbx=SOURCE/'SM_ControlStick.fbx'
sha=hashlib.sha256(fbx.read_bytes()).hexdigest()
if sha!=manifest['artifact_sha256']['SM_ControlStick.fbx'] or sha!=expected['sourceSha256']:
    raise RuntimeError('Control source digest differs from validated source')
original=unreal.load_asset('/Game/Star/Art/ExplorerV2/Parts/SM_ControlStick')
if not isinstance(original,unreal.StaticMesh):raise RuntimeError('Original control missing')
materials={}
for slot in original.get_editor_property('static_materials'):
    key=str(slot.get_editor_property('imported_material_slot_name'))
    if key not in manifest['materials_existing_slots']:key=str(slot.get_editor_property('material_slot_name'))
    material=slot.get_editor_property('material_interface')
    if key in manifest['materials_existing_slots'] and material:materials[key]=material.get_path_name()
for key in set(manifest['materials_existing_slots'])-set(materials):
    # New collar/boot surfaces use existing shared spacecraft finishes that
    # the simpler original control did not itself reference.
    material=unreal.load_asset('/Game/Star/Art/PhotoShip/Materials/'+key+'_Photo')
    if not isinstance(material,unreal.MaterialInterface):raise RuntimeError('Shared control finish missing: '+key)
    materials[key]=material.get_path_name()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
package='/Game/Star/Art/CockpitControlsV4/SM_ControlStick'
task=dict(kind='static_mesh',source=str(fbx),sha256=sha,package=package,object=package+'.SM_ControlStick',
          settings=dict(part=dict(materials=list(materials)),nanite=False),provenance=dict(source=str(fbx.relative_to(ROOT)),sha256=sha,design='Original fictional ergonomic control'))
asset,action=bootstrap.import_asset(task)
if asset.get_num_triangles(0)!=expected['expectedRenderTriangles']:
    raise RuntimeError('Control render topology differs from predicted FBX import')
if Path(asset.get_editor_property('asset_import_data').get_first_filename()).resolve()!=fbx.resolve():
    raise RuntimeError('Control native import source path differs')
bootstrap.configure_asset(task,asset,materials)
bounds=asset.get_bounds();actual=dict(min=[],max=[])
for i,axis in enumerate(('x','y','z')):
    center=getattr(bounds.origin,axis);extent=getattr(bounds.box_extent,axis)
    for edge,value in (('min',center-extent),('max',center+extent)):
        if abs(value-validation['new']['bounds_m'][edge][i]*100)>0.1:
            raise RuntimeError('Control local-pivot bounds mismatch')
        actual[edge].append(value)
bootstrap.save(asset)
runtime_path=ROOT/'Content/Star/Data/runtime_assets.json'
runtime=json.loads(runtime_path.read_text(encoding='utf-8'))
parts=[p for p in runtime['shipParts'] if p['name']=='SM_ControlStick']
if len(parts)!=1:raise RuntimeError('Ambiguous control runtime binding')
if any(abs(a-b)>0.01 for a,b in zip(parts[0]['locationCm'],manifest['runtime_location_cm'])):
    raise RuntimeError('Existing animated control pivot differs')
parts[0]['asset']=asset.get_path_name()
bootstrap.write_json(runtime_path,runtime)
bootstrap.write_json(ROOT/'work/v04/controls-import.json',dict(status='IMPORTED_AND_BOUND',asset=asset.get_path_name(),
    sourceSha256=sha,renderTriangles=asset.get_num_triangles(0),boundsCm=actual,materials=materials,locationCm=parts[0]['locationCm'],action=action))
