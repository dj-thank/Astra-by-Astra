"""Repair only generated material graphs in a dedicated UE Editor process."""
from pathlib import Path
import importlib.util
import json
import os
import runpy
from datetime import datetime, timezone
import unreal

root = Path(__file__).resolve().parents[2]
assert Path(unreal.Paths.project_dir()).resolve() == root
os.environ["STAR_BOOTSTRAP_MODE"] = "ship-materials"
runpy.run_path(str(root / "Tools/Unreal/bootstrap.py"), run_name="__main__")
spec = importlib.util.spec_from_file_location("star_planet_material_repair", root / "Tools/Unreal/Materials/create_materials.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
plan = json.loads((root / "work/import/source-plan.json").read_text(encoding="utf-8"))
result = builder.build_materials(plan["planetTextures"])
graphs = []
for path in result["materials"].values():
    material = unreal.load_asset(path)
    expressions = list(unreal.MaterialEditingLibrary.get_material_expressions(material))
    parameter_types = (unreal.MaterialExpressionScalarParameter, unreal.MaterialExpressionVectorParameter,
                       unreal.MaterialExpressionTextureObjectParameter)
    names = [str(e.get_editor_property("parameter_name")) for e in expressions if isinstance(e, parameter_types)]
    assert len(names) == len(set(names)), path + " has duplicate parameter nodes"
    graphs.append({"asset": material.get_path_name(), "expression_count": len(expressions), "parameter_names": names})
(root / "work/import/planet-material-repair-result.json").write_text(json.dumps({
    "status": "MATERIAL_GRAPHS_SAVED_READBACK_VERIFIED", "finishedAt": datetime.now(timezone.utc).isoformat(),
    "result": result, "graphs": graphs, "shader_game_render": "pending"}, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("STAR ship and planet material repair completed; game render verification pending")
