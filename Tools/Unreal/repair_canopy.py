"""Update only the owned closed-plate cockpit glass in a dedicated editor."""
from pathlib import Path
import json
import sys
import unreal

root=Path(__file__).resolve().parents[2]
assert Path(unreal.Paths.project_dir()).resolve()==root
sys.path.insert(0,str(root/'Tools/Unreal'))
import bootstrap

plan=json.loads((root/'work/import/source-plan.json').read_text(encoding='utf-8-sig'))
readbacks=[]
bootstrap.create_ship_materials(plan,names={'M_CanopyGlass'},force=True,readbacks=readbacks)
assert len(readbacks)==1
result={'status':'ASSET_SAVED_READBACK_ONLY','readbacks':readbacks,
        'recipe':{'baseColor':[0,0,0],'transmittance':[.995,.995,.995],'opacity':0,
                  'twoSided':False,'roughness':.012,'specular':.15},
        'note':'Neutral weak absorption and low-reflection approximation, not measured optical glass data; actual cook/render still required.'}
(root/'work/import/canopy-repair-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
unreal.log(json.dumps(result))
