"""Reapply only the owned PhotoShip texture recipe after the first real cook."""
from pathlib import Path
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bootstrap
import import_checks

bootstrap.require_dedicated_editor(ROOT)
path = ROOT / "Tools/Lookdev/PhotoShip/author_unreal.py"
spec = importlib.util.spec_from_file_location("photo_author", path)
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)
plan = import_checks.build_photo_plan(ROOT)
results = [author.import_photo_texture(task) for task in plan["tasks"]
           if task["kind"] == "photo_texture" and "/PhotoShip/Textures/" in task["package"]]
bootstrap.write_json(ROOT / "work/photo-pass/photo-mip-repair.json",
    dict(status="TEXTURE_RECIPE_SAVED", count=len(results), textures=results,
         processing="UE stretch full crop to POT and generate mips; no new observed detail",
         source_photos_unchanged=True, physical_repeat_unchanged=True))
