"""Import original reusable STAR sound assets in a dedicated editor commandlet."""
from pathlib import Path
import json,os,unreal
ROOT=Path(__file__).resolve().parents[2]
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
manifest=json.loads((ROOT/'Content/Star/Audio/audio-manifest.json').read_text(encoding='utf-8'))
for entry in manifest['items']:
 task=unreal.AssetImportTask();task.filename=str(ROOT/entry['file']);task.destination_path='/Game/Star/Audio'
 task.destination_name='SW_'+entry['id'];task.automated=True;task.replace_existing=True;task.save=True
 unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
 asset=unreal.load_asset('/Game/Star/Audio/SW_'+entry['id']);assert isinstance(asset,unreal.SoundWave)
 asset.set_editor_property('looping',entry['seconds']>=12)
 unreal.EditorAssetLibrary.save_loaded_asset(asset)
 assert abs(asset.get_editor_property('duration')-entry['seconds'])<0.05
out=ROOT/'work/public-audio-receipt.json';out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps({'runId':os.environ.get('STAR_AUTHOR_RUN_ID'),'imported':len(manifest['items']),'license':'MIT','standaloneRedistribution':True},indent=2),encoding='utf-8')
