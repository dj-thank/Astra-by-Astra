"""Run scoped quality authoring after base bootstrap, in a dedicated Editor."""
from pathlib import Path
import runpy
import unreal

root=Path(__file__).resolve().parents[2]
assert Path(unreal.Paths.project_dir()).resolve()==root
runpy.run_path(str(root/'Tools/Unreal/import_clouds.py'),run_name='__main__')
runpy.run_path(str(root/'Tools/Lookdev/Cockpit/author_unreal.py'),run_name='__main__')
unreal.log('STAR lookdev authoring completed; actual rendered quality remains to be checked')
