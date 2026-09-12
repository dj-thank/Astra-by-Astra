"""Create only new SolarMotion assets; never resave shared legacy content."""
from pathlib import Path
import sys,hashlib,shutil
import unreal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Unreal'))
import bootstrap as b
from Materials import create_materials as lib
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
DEST='/Game/Star/SolarMotion'
shutil.copy2(ROOT/'Data/SolarMotion/curves.json',ROOT/'Content/Star/Data/solar-motion-curves.json')
textures={}
for name,file in [('AiaTex','sun-304.jpg'),('HmiTex','sun-white.jpg'),('QualityTex','quality-white.png')]:
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(ROOT/'Data/SolarMotion'/file),destination_path=DEST,destination_name=name,automated=True,replace_existing=True,save=True).items():task.set_editor_property(k,v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex=unreal.load_asset(DEST+'/'+name);assert tex
    tex.set_editor_property('virtual_texture_streaming',False)
    tex.set_editor_property('srgb',name!='QualityTex')
    tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS if name=='QualityTex' else unreal.TextureCompressionSettings.TC_DEFAULT)
    unreal.EditorAssetLibrary.save_loaded_asset(tex);textures[name]=tex

def inputs(material,surface):
    x={'UV':b.node(material,unreal.MaterialExpressionTextureCoordinate)}
    for name,value in {'SolarDetail':0.0,'SolarTime':0.0,'SolarScaleCm':1.0,'SunRadiance':18000000.0,'EarthRadiusMeters':0.0}.items():
        x[name]=b.node(material,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
    if surface:
        x['CameraVector']=b.node(material,unreal.MaterialExpressionCameraVectorWS)
        x['SurfaceNormal']=b.node(material,unreal.MaterialExpressionPixelNormalWS)
        for name,value in {'EarthCameraLocal':(0,0,3),'EarthAxisX':(1,0,0),'EarthAxisY':(0,-1,0),'EarthAxisZ':(0,0,1)}.items():
            x[name]=b.node(material,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*value,1))
        for name,tex in textures.items():x[name]=b.node(material,unreal.MaterialExpressionTextureObjectParameter,parameter_name=name,texture=tex)
    else:
        x['Seed']=b.node(material,unreal.MaterialExpressionVertexColor)
        x['RadialNormal']=b.node(material,unreal.MaterialExpressionVertexNormalWS)
    return x

for name,surface in [('M_SolarSurface',True),('M_SolarPlasma',False)]:
    source=(ROOT/'Shaders/Star'/('SolarSurface.ush' if surface else 'SolarPlasma.ush')).read_text(encoding='utf-8')
    fingerprint=hashlib.sha256((source+(ROOT/'Shaders/Star/SolarPlasmaOffset.ush').read_text()+'pins-v2').encode()).hexdigest()
    material,changed=b.create_owned_material(DEST+'/'+name,fingerprint)
    if not changed:continue
    material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE if surface else unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('two_sided',not surface)
    x=inputs(material,surface)
    if surface:
        expr=lib._custom(material,'SolarSurface.ush',x,3)
        baseline=lib.shader_source('Sun.ush').replace('return max(SunRadiance,0.0)*limb*tint;','float3 baseSun=max(SunRadiance,0.0)*limb*tint;')
        expr.set_editor_property('code',baseline+'\n'+source)
        b.property_input(expr,'MP_EMISSIVE_COLOR')
    else:
        expr=lib._custom(material,'SolarPlasma.ush',{k:v for k,v in x.items() if k!='RadialNormal'},4)
        unreal.MaterialEditingLibrary.connect_material_property(expr,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        alpha=b.node(material,unreal.MaterialExpressionComponentMask,r=False,g=False,b=False,a=True)
        assert unreal.MaterialEditingLibrary.connect_material_expressions(expr,'',alpha,'')
        b.property_input(alpha,'MP_OPACITY')
        offset=lib._custom(material,'SolarPlasmaOffset.ush',x,3)
        b.property_input(offset,'MP_WORLD_POSITION_OFFSET')
    unreal.MaterialEditingLibrary.recompile_material(material)
    b.stamp(material,fingerprint,{'scope':'SolarMotion only','model':'Observed maps + artistic UTC motion'})
    b.save(material)
unreal.log('STAR SolarMotion assets saved')
