"""Readable world-space VR UI, independent of scene exposure; no world-image edits."""
from pathlib import Path
import sys,json,unreal
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Tools/Unreal'));import bootstrap
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
path='/Game/Star/Materials/M_VRPanel'
material=unreal.load_asset(path)
if material:
 assert unreal.EditorAssetLibrary.get_metadata_tag(material,'StarVRPanel')=='v1'
else:
 source=unreal.load_asset('/Engine/EngineMaterials/Widget3DPassThrough_Translucent')
 while isinstance(source,unreal.MaterialInstanceConstant):source=source.get_editor_property('parent')
 assert isinstance(source,unreal.Material)
 material=unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(),path);assert material
 material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
 material.set_editor_property('two_sided',True)
 emissive=unreal.MaterialEditingLibrary.get_material_property_input_node(material,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 emissive_out=unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(material,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 opacity=unreal.MaterialEditingLibrary.get_material_property_input_node(material,unreal.MaterialProperty.MP_OPACITY)
 opacity_out=unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(material,unreal.MaterialProperty.MP_OPACITY)
 if opacity is None:
  opacity=unreal.MaterialEditingLibrary.get_material_property_input_node(material,unreal.MaterialProperty.MP_OPACITY_MASK)
  opacity_out=unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(material,unreal.MaterialProperty.MP_OPACITY_MASK)
 assert emissive and opacity
 inputs={'UV':bootstrap.node(material,unreal.MaterialExpressionTextureCoordinate),'PointerUV':bootstrap.node(material,unreal.MaterialExpressionVectorParameter,parameter_name='PointerUV',default_value=unreal.LinearColor(.5,.5,0,1)),'PointerVisible':bootstrap.node(material,unreal.MaterialExpressionScalarParameter,parameter_name='PointerVisible',default_value=0.0)}
 dot=bootstrap.node(material,unreal.MaterialExpressionCustom,description='STAR VR gaze dot',output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
 pins=[]
 for name in inputs:
  pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
 dot.set_editor_property('inputs',pins)
 for name,node in inputs.items():bootstrap.connect(node,dot,name)
 dot.set_editor_property('code','return saturate(PointerVisible)*(1.0-smoothstep(2.0,3.5,length((UV-PointerUV.xy)*float2(1920,1080))));')
 color=bootstrap.node(material,unreal.MaterialExpressionLinearInterpolate,const_b=1.0)
 bootstrap.connect(emissive,color,'A',emissive_out);bootstrap.connect(dot,color,'Alpha')
 exposure=bootstrap.node(material,unreal.MaterialExpressionEyeAdaptation)
 safe=bootstrap.node(material,unreal.MaterialExpressionMax,const_b=0.000001);bootstrap.connect(exposure,safe,'A')
 normalized=bootstrap.node(material,unreal.MaterialExpressionDivide);bootstrap.connect(color,normalized,'A');bootstrap.connect(safe,normalized,'B')
 alpha=bootstrap.node(material,unreal.MaterialExpressionMax);bootstrap.connect(opacity,alpha,'A',opacity_out);bootstrap.connect(dot,alpha,'B')
 unreal.MaterialEditingLibrary.connect_material_property(normalized,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 unreal.MaterialEditingLibrary.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY)
 material.set_editor_property('disable_depth_test',True)
 unreal.EditorAssetLibrary.set_metadata_tag(material,'StarVRPanel','v1')
 unreal.MaterialEditingLibrary.recompile_material(material);bootstrap.save(material)
(ROOT/'work/earth-time/vr-panel-author.json').write_text(json.dumps({'material':path,'exposureCompensatedUI':True,'depthTestDisabledForUIOnly':True,'gazeDot':True,'headsetVerified':False},indent=2),encoding='utf-8')
