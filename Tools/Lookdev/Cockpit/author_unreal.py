"""Dedicated UE5.8 editor authoring. Run with -ExecutePythonScript or exec(open()).

Creates owned material clones and textures, changes ONLY scoped mesh material slots.
Does not alter base materials, mesh geometry, actors, camera, engine or lighting.
Python wrapper and shaders require root's real UE execution; no simulated PASS.
"""
from pathlib import Path
import json
import hashlib
import unreal

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
RECIPE = json.loads((HERE/'recipe.json').read_text(encoding='utf-8-sig'))
DEST = '/Game/Star/Art/Lookdev/Cockpit'
PARTS = '/Game/Star/Art/ExplorerV2/Parts'
BASES = '/Game/Star/Art/ExplorerV2/Materials'
TAG = 'STAR_CockpitLookdevOwner'
OWNER = 'cockpit-object-triplanar-v1'
ME = unreal.MaterialEditingLibrary
EA = unreal.EditorAssetLibrary


def node(m, cls, **props):
    n = ME.create_material_expression(m, cls, -600, 0)
    if n is None: raise RuntimeError(f'Cannot create {cls}')
    for k,v in props.items(): n.set_editor_property(k,v)
    return n


def wire(src, dst, pin='', output=''):
    if not ME.connect_material_expressions(src,output,dst,pin):
        raise RuntimeError(f'Connection failed: {src.get_name()} -> {pin}')


def prop(m, src, name):
    p = getattr(unreal.MaterialProperty,name)
    if not ME.connect_material_property(src,'',p): raise RuntimeError(name)
    if ME.get_material_property_input_node(m,p) != src:
        raise RuntimeError(f'Property readback failed: {name}')


def custom(m, code, inputs, count=3):
    n=node(m,unreal.MaterialExpressionCustom,code=code,
           output_type=getattr(unreal.CustomMaterialOutputType,'CMOT_FLOAT'+(str(count) if count>1 else '1')))
    pins=[]
    for key in inputs:
        p=unreal.CustomInput(); p.set_editor_property('input_name',key); pins.append(p)
    n.set_editor_property('inputs',pins)
    for key,value in inputs.items(): wire(value,n,key)
    return n


def scalar(m, value): return node(m,unreal.MaterialExpressionConstant,r=float(value))


def vector(m, values):
    return node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*values,1))


def vector_transform(m, src, source, dest):
    n=node(m,unreal.MaterialExpressionTransform,
           transform_source_type=getattr(unreal.MaterialVectorCoordTransformSource,'TRANSFORMSOURCE_'+source),
           transform_type=getattr(unreal.MaterialVectorCoordTransform,'TRANSFORM_'+dest))
    wire(src,n); return n


def coordinates(m):
    # LWC world-to-local is performed by UE's transform node BEFORE float custom HLSL.
    wp=node(m,unreal.MaterialExpressionWorldPosition,
            world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    lp=node(m,unreal.MaterialExpressionTransformPosition,
            transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
            transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    wire(wp,lp)
    meters=custom(m,'return P * 0.01;',{'P':lp})
    # VertexInterpolator prevents normal-property feedback and supports pixel inputs.
    vn=node(m,unreal.MaterialExpressionVertexNormalWS)
    ln=vector_transform(m,vn,'WORLD','LOCAL')
    vi=node(m,unreal.MaterialExpressionVertexInterpolator); wire(ln,vi,'VS')
    normal=custom(m,'return normalize(N);',{'N':vi})
    weights=custom(m,'float3 w=pow(abs(N),4.0); return w/max(dot(w,1.0),1e-6);',{'N':normal})
    return meters,normal,weights


def import_texture(filename, srgb, compression):
    path=f'{DEST}/Textures/{Path(filename).stem}'
    asset=unreal.load_asset(path)
    source=ROOT/RECIPE['texture_root']/filename
    source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
    if asset is not None and (not isinstance(asset,unreal.Texture2D) or EA.get_metadata_tag(asset,TAG)!=OWNER):
        raise RuntimeError(f'Existing unowned or incompatible texture {path}')
    if asset is None or EA.get_metadata_tag(asset,'STAR_CockpitTextureSHA256')!=source_hash:
        task=unreal.AssetImportTask()
        for k,v in {'filename':str(ROOT/RECIPE['texture_root']/filename),
                    'destination_path':f'{DEST}/Textures','destination_name':Path(filename).stem,
                    'automated':True,'replace_existing':asset is not None,'replace_existing_settings':True,'save':False}.items():
            task.set_editor_property(k,v)
        factory=unreal.TextureFactory(); factory.set_editor_property('create_material',False)
        task.set_editor_property('factory',factory)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        asset=unreal.load_asset(path)
        if not isinstance(asset,unreal.Texture2D): raise RuntimeError(f'Import failed {path}')
        EA.set_metadata_tag(asset,TAG,OWNER)
        EA.set_metadata_tag(asset,'STAR_CockpitTextureSHA256',source_hash)
    settings={'srgb':srgb,'compression_settings':compression,'flip_green_channel':False,
              'virtual_texture_streaming':False,'address_x':unreal.TextureAddress.TA_WRAP,
              'address_y':unreal.TextureAddress.TA_WRAP,'lod_bias':0}
    for k,v in settings.items():
        asset.set_editor_property(k,v)
        if asset.get_editor_property(k)!=v: raise RuntimeError(f'Texture readback {k}')
    if not EA.save_loaded_asset(asset,False): raise RuntimeError(f'Texture save failed {path}')
    if EA.get_metadata_tag(asset,'STAR_CockpitTextureSHA256')!=source_hash: raise RuntimeError(f'Texture source hash mismatch {path}')
    return asset


def samples(m, tex, sampler, p, n):
    result=[]
    # Signed, right-handed U/V bases. UV0/UV1 are never read or modified.
    for code in ('float2(P.y*(N.x<0?-1:1),P.z)',
                 'float2(P.z*(N.y<0?-1:1),P.x)',
                 'float2(P.x*(N.z<0?-1:1),P.y)'):
        uv=custom(m,f'return {code}/Repeat;',{'P':p,'N':n,'Repeat':scalar(m,.25)},2)
        s=node(m,unreal.MaterialExpressionTextureSample,texture=tex,sampler_type=sampler)
        wire(uv,s,'UVs'); result.append(s)
    return result


def blend(m, values, w, scalar_output=False):
    code='return (X*W.x+Y*W.y+Z*W.z)'+('.r;' if scalar_output else '.rgb;')
    return custom(m,code,dict(zip(['X','Y','Z','W'],[*values,w])),1 if scalar_output else 3)


def build(m, name, textures):
    for old in list(ME.get_material_expressions(m)): ME.delete_material_expression(m,old)
    assert not list(ME.get_material_expressions(m)), 'Graph clear left expressions'
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('tangent_space_normal',False) # Normal AND tangent outputs in world space
    m.set_editor_property('two_sided',False)
    ME.set_base_material_usage(m,unreal.MaterialUsage.MATUSAGE_NANITE,True)
    assert ME.has_material_usage(m,unreal.MaterialUsage.MATUSAGE_NANITE)
    p,n,w=coordinates(m)
    spec=RECIPE['materials'][name]
    if name=='M_CockpitSoftTouch':
        base=blend(m,samples(m,textures['base'],unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,p,n),w)
        rough=blend(m,samples(m,textures['rough'],unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,p,n),w,True)
        ns=samples(m,textures['normal'],unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,p,n)
        # Normal texture samplers already return unpacked [-1,1] vectors, including BC5 Z.
        # Add tangent-plane slopes to true geometric N. Flat maps preserve N exactly,
        # even on beveled faces where dominant-axis weighted normals would be wrong.
        local_normal=custom(m,'''
float3 sx=X.xyz/max(X.z,0.2), sy=Y.xyz/max(Y.z,0.2), sz=Z.xyz/max(Z.z,0.2);
float3 d = W.x*float3(0,sx.x*(N.x<0?-1:1),sx.y)
         + W.y*float3(sy.y,0,sy.x*(N.y<0?-1:1))
         + W.z*float3(sz.x*(N.z<0?-1:1),sz.y,0);
d -= N*dot(N,d);
return normalize(N+d);
''',dict(zip(['X','Y','Z','N','W'],[*ns,n,w])))
    else:
        base=vector(m,spec['base_color_linear']); rough=scalar(m,spec['roughness'])
        local_normal=n
    prop(m,base,'MP_BASE_COLOR'); prop(m,rough,'MP_ROUGHNESS')
    prop(m,scalar(m,spec['metallic']),'MP_METALLIC')
    prop(m,scalar(m,spec.get('unreal_specular',.5)),'MP_SPECULAR')
    world_normal=vector_transform(m,local_normal,'LOCAL','WORLD')
    prop(m,world_normal,'MP_NORMAL')
    # Analytic rough conductor: orientation survives object rotation. No grunge/noise.
    axis=(0,1,0) if name=='M_Titanium' else (1,0,0)
    tangent=custom(m,'''
float3 t=A-N*dot(A,N);
if(dot(t,t)<0.01){float3 b=abs(N.z)<0.9?float3(0,0,1):float3(0,1,0);t=b-N*dot(b,N);}
return normalize(t);
''',{'A':vector(m,axis),'N':n})
    prop(m,vector_transform(m,tangent,'LOCAL','WORLD'),'MP_TANGENT')
    prop(m,scalar(m,spec.get('anisotropy',0)),'MP_ANISOTROPY')
    ME.recompile_material(m)
    for key in ['MP_BASE_COLOR','MP_ROUGHNESS','MP_NORMAL','MP_METALLIC','MP_TANGENT','MP_ANISOTROPY']:
        assert ME.get_material_property_input_node(m,getattr(unreal.MaterialProperty,key)) is not None,key
    assert m.get_editor_property('tangent_space_normal') is False
    assert ME.has_material_usage(m,unreal.MaterialUsage.MATUSAGE_NANITE)
    EA.save_loaded_asset(m,False)
    return {'material':m.get_path_name(),'normal_connected':True,'tangent_space_normal':False,
            'nanite_usage_readback':True,'expressions':len(list(ME.get_material_expressions(m))),
            'shader_compile_status':'REQUESTED_NOT_ASSERTED; inspect actual UE shader log and game'}


def slot_paths(mesh):
    return [s.get_editor_property('material_interface').get_path_name()
            if s.get_editor_property('material_interface') else None
            for s in mesh.get_editor_property('static_materials')]


def main():
    manifests=json.loads((ROOT/'Art/Explorer/V2/art_manifest.json').read_text(encoding='utf-8'))
    meshes={p['name']:unreal.load_asset(f"{PARTS}/{p['name']}") for p in manifests['model_parts']}
    for name,mesh in meshes.items():
        if not isinstance(mesh,unreal.StaticMesh): raise RuntimeError(f'Missing mesh {name}')
    before={k:slot_paths(v) for k,v in meshes.items()}
    bases={k:unreal.load_asset(f'{BASES}/{k}') for k in RECIPE['materials']}
    for k,m in bases.items():
        if not isinstance(m,unreal.Material): raise RuntimeError(f'Missing base material {k}')
    base_expression_counts={k:len(list(ME.get_material_expressions(v))) for k,v in bases.items()}
    textures={'base':import_texture('SoftTouch_BaseColor.png',True,unreal.TextureCompressionSettings.TC_DEFAULT),
              'rough':import_texture('SoftTouch_Roughness.png',False,unreal.TextureCompressionSettings.TC_MASKS),
              'normal':import_texture('SoftTouch_NormalDX.png',False,unreal.TextureCompressionSettings.TC_NORMALMAP)}
    clones={}; receipts=[]
    for name,base in bases.items():
        destination=f'{DEST}/Materials/{name}_Cockpit'
        m=unreal.load_asset(destination)
        if m is None:
            EA.make_directory(f'{DEST}/Materials')
            m=EA.duplicate_asset(base.get_path_name(),destination)
            if m is None: raise RuntimeError(f'Clone failed {destination}')
            EA.set_metadata_tag(m,TAG,OWNER)
        elif EA.get_metadata_tag(m,TAG)!=OWNER: raise RuntimeError(f'Unowned clone {destination}')
        receipts.append(build(m,name,textures)); clones[name]=m
    changed=[]
    try:
        for name in RECIPE['scope_parts']:
            mesh=meshes[name]
            for i,slot in enumerate(mesh.get_editor_property('static_materials')):
                sid=str(slot.get_editor_property('imported_material_slot_name'))
                if sid not in clones: sid=str(slot.get_editor_property('material_slot_name'))
                if sid in clones:
                    mesh.set_material(i,clones[sid])
                    assert slot_paths(mesh)[i]==clones[sid].get_path_name()
                    changed.append({'part':name,'slot':i,'material_id':sid,'before':before[name][i],'after':slot_paths(mesh)[i]})
            EA.save_loaded_asset(mesh,False)
        assert changed, 'No scoped slots matched'
        for name,mesh in meshes.items():
            after=slot_paths(mesh)
            allowed_indices={x['slot'] for x in changed if x['part']==name}
            assert len(after)==len(before[name])
            for i,value in enumerate(after):
                if i not in allowed_indices: assert value==before[name][i],(name,i)
        for k,base in bases.items():
            assert len(list(ME.get_material_expressions(base)))==base_expression_counts[k]
    except Exception:
        for name in RECIPE['scope_parts']:
            for i,path in enumerate(before[name]): meshes[name].set_material(i,unreal.load_asset(path) if path else None)
            EA.save_loaded_asset(meshes[name],False)
        raise
    receipt={'status':'AUTHORING_READBACK_ONLY','materials':receipts,'assignments':changed,
             'non_target_slots_unchanged':True,'normal_connected':True,'uv_dependency':'none',
             'repeat_m':.25,'shader_compile':'NOT_VERIFIED','packaged_comparison':'NOT_RUN'}
    out=ROOT/'work/validation/cockpit-lookdev'; out.mkdir(parents=True,exist_ok=True)
    (out/'authoring_readback.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    unreal.log(json.dumps(receipt))


if __name__=='__main__': main()
