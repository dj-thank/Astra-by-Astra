"""Root-owned additive cockpit side lining import; existing meshes untouched."""
from pathlib import Path
import hashlib
import json
import sys
import unreal

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bootstrap

def run(output_directory=None, material_directory="/Game/Star/Art/CockpitV3/Materials"):
    spec=json.loads((ROOT/"Data/cockpit_v3_side_details.json").read_text(encoding="utf-8"))
    source=ROOT/"Content/Star/Art/CockpitV3"/spec["fbx"]
    sha=hashlib.sha256(source.read_bytes()).hexdigest()
    name=spec["name"];package="/Game/Star/Art/CockpitV3/Parts/"+name
    if sha!=spec["sha256"]:raise ValueError("Side lining source hash mismatch")
    material_names=spec["materials"]
    materials={key:material_directory+"/"+key for key in material_names}
    for path in materials.values():
        if not isinstance(unreal.load_asset(path),unreal.MaterialInterface):raise RuntimeError("Missing side lining material "+path)
    task=dict(kind="static_mesh",source=str(source),sha256=sha,package=package,object=package+"."+name,
        settings=dict(part=dict(materials=material_names),nanite=False),provenance=dict(source=spec["fbx"],sha256=sha,design="fictional ASTER cabin lining; NASA reference"))
    asset,action=bootstrap.import_asset(task)
    bootstrap.configure_asset(task,asset,materials)
    bounds=asset.get_bounds()
    actual={"min":[],"max":[]}
    for i,key in enumerate(("x","y","z")):
        center=getattr(bounds.origin,key);extent=getattr(bounds.box_extent,key)
        for edge,value in (("min",center-extent),("max",center+extent)):
            if abs(value-spec["bounds_ue_cm"][edge][i])>0.1:raise RuntimeError("Side lining UE bounds mismatch")
            actual[edge].append(value)
    bootstrap.save(asset)
    path=ROOT/"Content/Star/Data/runtime_assets.json"
    runtime=json.loads(path.read_text(encoding="utf-8"))
    existing=[p for p in runtime["shipParts"] if p["name"]==name]
    if not existing:runtime["shipParts"].append(dict(name=name,asset=asset.get_path_name(),role="cockpit",locationCm=[0,0,0]))
    elif len(existing)!=1 or existing[0]["asset"]!=asset.get_path_name():raise RuntimeError("Unexpected side lining binding")
    bootstrap.write_json(path,runtime)
    result=dict(status="IMPORTED_BOUNDS_VERIFIED",asset=asset.get_path_name(),sourceSha256=sha,bounds_ue_cm=actual,action=action)
    bootstrap.write_json((Path(output_directory) if output_directory is not None else ROOT/"work/v03")/"side-import.json",result)
    return result

if __name__=="__main__":
    bootstrap.require_dedicated_editor(ROOT)
    run()
