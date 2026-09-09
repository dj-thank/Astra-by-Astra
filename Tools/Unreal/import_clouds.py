"""Import only the proven NASA cloud texture and rebind generated planet materials."""
from pathlib import Path
import importlib.util
import json
from datetime import datetime, timezone
import sys
import unreal

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "Tools/Unreal"))
import bootstrap
bootstrap.require_dedicated_editor(root)
plan = bootstrap.build_plan(root, root / "outputs/audio/STAR-Audio-Pack-v1")
readbacks=[]
for task in plan["tasks"]:
    if task["package"] != "/Game/Star/Textures/earth_clouds_8k" and not task["package"].startswith("/Game/Star/Art/Lookdev/Lunar/"):
        continue
    asset, action = bootstrap.import_asset(task)
    bootstrap.configure_asset(task, asset, {})
    bootstrap.save(asset)
    readback = bootstrap.readback(task, asset)
    for field in ("address_x", "address_y"):
        assert asset.get_editor_property(field) == getattr(unreal.TextureAddress, task["settings"][field])
    readbacks.append({**readback,"action":action})
asset=unreal.load_asset("/Game/Star/Textures/earth_clouds_8k")
spec = importlib.util.spec_from_file_location("star_cloud_materials", root / "Tools/Unreal/Materials/create_materials.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
materials = builder.build_materials(plan["planetTextures"])
earth = unreal.load_asset("/Game/Star/Materials/MI_Earth")
bound = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(earth, "CloudTex")
enabled = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(earth, "UseCloudTexture")
assert bound.get_path_name() == asset.get_path_name() and enabled == 1.0
bootstrap.write_json(root / "work/import/source-plan.json", plan)
bootstrap.write_json(root / "work/import/earth-clouds-import-result.json", {
    "status": "TEXTURE_AND_MATERIAL_BINDING_VERIFIED", "finishedAt": datetime.now(timezone.utc).isoformat(),
    "textures": readbacks, "materials": materials,
    "boundCloud": bound.get_path_name(), "useCloudTexture": enabled, "gameRender": "pending"})
