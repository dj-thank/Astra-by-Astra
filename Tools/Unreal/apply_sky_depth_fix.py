"""Stars contribute light but must not fill the sky depth buffer."""
from pathlib import Path
import json,sys,unreal
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():raise RuntimeError('Fresh commandlet required')
m=unreal.load_asset('/Game/Star/Materials/M_Stars');assert m
m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('disable_depth_test',False)
unreal.MaterialEditingLibrary.recompile_material(m);bootstrap.save(m)
r={'material':m.get_path_name(),'blend':str(m.get_editor_property('blend_mode')),'disableDepthTest':bool(m.get_editor_property('disable_depth_test')),'reason':'An opaque star sphere fills depth even beyond the atmosphere. Native solar disk rendering requires far/empty sky depth. Additive stars preserve celestial occlusion without writing sky depth.'}
(ROOT/'work/scenic-flight/sky-depth-author.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.log('STAR additive star background saved')
