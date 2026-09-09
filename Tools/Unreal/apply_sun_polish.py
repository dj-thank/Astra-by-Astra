"""Rebuild only the Sun graph and refresh the atmosphere Custom code."""
from pathlib import Path
import sys
import unreal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap
from Materials import create_materials as library
# This script runs in a fresh Python commandlet. LevelEditorSubsystem's
# PIE query dereferences an absent editor viewport in commandlet mode.
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
    raise RuntimeError('Dedicated clean authoring process required')
bootstrap.create_sun()
atmosphere=unreal.load_asset('/Game/Star/Materials/M_Atmosphere')
if unreal.EditorAssetLibrary.get_metadata_tag(atmosphere,library.OWNER_TAG)!=library.GENERATOR:
    raise RuntimeError('Atmosphere ownership mismatch')
nodes=[n for n in unreal.MaterialEditingLibrary.get_material_expressions(atmosphere)
       if isinstance(n,unreal.MaterialExpressionCustom) and n.get_editor_property('description')=='STAR Atmosphere']
assert len(nodes)==1, 'Expected one atmosphere shader'
nodes[0].set_editor_property('code',library.shader_source('Atmosphere.ush'))
unreal.MaterialEditingLibrary.recompile_material(atmosphere)
bootstrap.save(atmosphere)
unreal.log('STAR Sun and atmosphere saved; GPU validation pending')
