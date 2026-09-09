"""Deterministic photo crops / PBR preparation; no generated detail or UE writes."""
from pathlib import Path
import json,hashlib
import numpy as np
from PIL import Image,ImageDraw,ImageFilter
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).parent
OUT=ROOT/'Content/Star/Art/PhotoShip'
SRC=OUT/'Sources'
MAP=OUT/'Maps';MAP.mkdir(exist_ok=True)
def read(path,mode,crop):
    im=Image.open(SRC/path).crop(crop)
    if mode=='L' and im.mode in ('I','I;16','I;16B','I;16L'):
        return np.asarray(im,dtype=np.float32)/65535
    return np.asarray(im.convert(mode),dtype=np.float32)/255
def periodic(a,width=16):
    a=a.copy()
    for axis in (0,1):
        a=np.swapaxes(a,0,axis)
        for i in range(width):
            t=.5*(1-i/width);lo=a[i].copy();hi=a[-i-1].copy()
            a[i]=lo*(1-t)+hi*t;a[-i-1]=hi*(1-t)+lo*t
        a=np.swapaxes(a,0,axis)
    return a
def write(name,a):
    Image.fromarray(np.rint(np.clip(a,0,1)*255).astype('uint8')).save(MAP/name)
def set_maps(name,color,rough,normal,crop,rough_mean,rough_gain,normal_gain):
    c=read(color,'RGB',crop);lum=c@np.array([.2126,.7152,.0722])
    # Grayscale modulation preserves original STAR color; this is adaptation, not measured albedo.
    mod=periodic(np.clip(.5+(lum-lum.mean())*.8,.3,.7))
    write(name+'_Modulation.png',mod)
    r=read(rough,'L',crop);write(name+'_Roughness.png',periodic(rough_mean+(r-r.mean())*rough_gain))
    n=read(normal,'RGB',crop)*2-1;n[...,:2]*=normal_gain;n=periodic(n)
    n/=np.maximum(np.linalg.norm(n,axis=2,keepdims=True),1e-8)
    write(name+'_NormalDX.png',n*.5+.5)
    Image.open(SRC/color).crop(crop).convert('RGB').save(MAP/(name+'_PhotoCrop.png'))
    return dict(id=name,source_color=color,crop_px=list(crop),roughness_mean=rough_mean,roughness_gain=rough_gain,normal_xy_gain=normal_gain,normal_evidence='publisher PBR map, adapted; not a measured STAR material',maps={k:f'Content/Star/Art/PhotoShip/Maps/{name}_{v}.png' for k,v in [('modulation','Modulation'),('roughness','Roughness'),('normal_dx','NormalDX')]})
sets=[]
sets.append(set_maps('SeatWeave','terlenka_Diffuse.png','terlenka_Rough.png','terlenka_nor_dx.png',(0,0,2048,2048),.82,.32,.6))
sets[-1].update(repeat_m=[.265708,.265651],scale_evidence='Poly Haven dimensions millimeters; 2048 of 2053 px height cropped')
sets.append(set_maps('SoftTouch','rubber_tiles_Diffuse.png','rubber_tiles_Rough.png','rubber_tiles_nor_dx.png',(64,64,320,320),.64,.16,.18))
sets[-1].update(repeat_m=[.25,.25],scale_evidence='adopted prior 2m tile field; material analogy to clean elastomer, flooring seams excluded')
sets.append(set_maps('SatinMetal','Metal063/Metal063_1K-JPG_Color.jpg','Metal063/Metal063_1K-JPG_Roughness.jpg','Metal063/Metal063_1K-JPG_NormalDX.jpg',(32,32,288,288),.32,.12,.12))
sets[-1].update(repeat_m=[.12,.12],scale_evidence='estimated render scale; publisher dimensions 0; steel surface analogy, not titanium/alloy chemistry nor brushing measurement')
sets.append(set_maps('EtchedMetal','Metal063/Metal063_1K-JPG_Color.jpg','Metal063/Metal063_1K-JPG_Roughness.jpg','Metal063/Metal063_1K-JPG_NormalDX.jpg',(32,32,288,288),.5,.2,.22))
sets[-1].update(repeat_m=[.12,.12],scale_evidence='same clean steel photo region, stronger satin response; not measured acid etch')
sets.append(set_maps('CeramicCoat','interior_tiles_Diffuse.png','interior_tiles_Rough.png','interior_tiles_nor_dx.png',(560,220,816,476),.42,.12,.08))
sets[-1].update(repeat_m=[.2375,.2375],scale_evidence='1.9m source field x 256/2048; crop entirely inside one ceramic face, excluding grout',material_analogy='glazed ceramic microstructure used for engineered ceramic coating; existing ship colors retained, not aerospace sample certification')
def single_photo_maps(name,file,crop,rough_mean,rough_gain,repeat):
    c=read(file,'RGB',crop);lum=c@np.array([.2126,.7152,.0722])
    # Remove smooth illumination field. Residual remains photo pixels, not noise.
    blur=np.asarray(Image.fromarray(np.rint(lum*255).astype('uint8')).filter(ImageFilter.GaussianBlur(6)),dtype=float)/255
    residual=lum-blur
    write(name+'_Modulation.png',periodic(.5+residual*.3,width=8))
    write(name+'_Roughness.png',periodic(rough_mean+residual*rough_gain,width=8))
    # One RGB photo cannot establish geometric normals. Flat fallback is explicitly analytic.
    flat=np.zeros((*lum.shape,3),dtype=float);flat[...,:2]=.5;flat[...,2]=1
    write(name+'_NormalDX.png',flat)
    Image.open(SRC/file).crop(crop).save(MAP/(name+'_PhotoCrop.png'))
    return dict(id=name,source_color=file,crop_px=list(crop),repeat_m=repeat,scale_evidence='estimated authoring span, no calibrated physical ruler in photo',roughness_mean=rough_mean,roughness_gain=rough_gain,normal_xy_gain=0,normal_evidence='analytic flat fallback; no single-image RGB-to-normal inference',roughness_evidence='photo luminance residual mapped to roughness as artistic estimate; not BRDF measurement',deillumination='subtract GaussianBlur radius 6px; only high frequency photographic residual used',maps={k:f'Content/Star/Art/PhotoShip/Maps/{name}_{v}.png' for k,v in [('modulation','Modulation'),('roughness','Roughness'),('normal_dx','NormalDX')]})
sets.append(single_photo_maps('CarbonShield','ParkerHeatShield.jpg',(320,428,576,476),.78,.4,[.5,.09375]))
sets.append(single_photo_maps('FoilResponse','CassiniHardware.jpg',(1028,399,1198,581),.34,.16,[.24,.25694]))
# Photo patch preserved only for optics/fold calibration: reflected room cannot become albedo.
refs=[('FoilFold','CassiniHardware.jpg',(1028,399,1198,581)),('RadiatorMirror','CassiniHardware.jpg',(907,226,1185,343)),('WhiteCoating','ParkerHeatShield.jpg',(390,380,980,399)),('BlackShield','ParkerHeatShield.jpg',(300,425,600,475))]
for name,file,box in refs:Image.open(SRC/file).crop(box).save(MAP/(name+'_ReferenceCrop.png'))
(HERE/'map_sets.json').write_text(json.dumps(sets,indent=2)+'\n',encoding='utf-8')
manifest=json.loads((ROOT/'Art/Explorer/V2/art_manifest.json').read_text(encoding='utf-8'))
bindings=[]
preserve={'M_CanopyGlass','M_DisplayBlack','M_PhosphorIce','M_LabelWhite','M_NavigationWhite','M_NavigationRed','M_NavigationGreen'}
assigned={'M_SeatWoven':'SeatWeave','M_CockpitSoftTouch':'SoftTouch','M_Titanium':'SatinMetal','M_MachinedAlloy':'SatinMetal','M_EtchedTitanium':'EtchedMetal','M_CeramicIvory':'CeramicCoat','M_CeramicWarm':'CeramicCoat','M_CeramicCold':'CeramicCoat','M_CeramicCool':'CeramicCoat','M_MutedAmber':'CeramicCoat','M_GraphiteStructure':'CarbonShield','M_HeatShield':'CarbonShield','M_HeatCopper':'SatinMetal','M_HeatBlue':'SatinMetal','M_ThermalFoil':'FoilResponse'}
bare_metals={'M_Titanium','M_MachinedAlloy','M_EtchedTitanium','M_HeatCopper','M_HeatBlue','M_ThermalFoil'}
for name,m in manifest['materials'].items():
    b=dict(material=name,parts=[p['name'] for p in manifest['model_parts'] if name in p['materials']],base_color_linear=m['base_color_linear'])
    if name in assigned:
        b.update(status='PHOTO_MAP_CANDIDATE' if b['parts'] else 'PHOTO_MAP_CANDIDATE_UNASSIGNED',map_set=assigned[name],metallic=1 if name in bare_metals else 0,base_color_equation='original_linear_color * (1 + (Modulation_linear - 0.5) * 0.3)')
        if name in ['M_HeatCopper','M_HeatBlue']:b.update(interpretation='photographic steel microstructure recolored to original copper/heat-blue design; not measured metal spectrum or observed heat history')
        if name=='M_MutedAmber':b.update(interpretation='amber dielectric coating with photographic glazed ceramic microstructure; not bare copper or gold')
        if name=='M_GraphiteStructure':b.update(interpretation='photographic carbon-composite shield surface analogy, not measured graphite hull coating')
        if name=='M_HeatShield':b.update(interpretation='Parker carbon shield side photographic residual; radiator instances use dark-coated-fin analogy, not NASA radiator construction',part_overrides={p:dict(roughness_offset=-.04,interpretation='dark radiator coating surface analogy, dynamic thermal emission remains runtime controlled') for p in ['SM_RadiatorPort','SM_RadiatorStarboard']})
    elif name in preserve:b.update(status='PRESERVE_FUNCTIONAL',reference_photo='ShuttleFlightDeck.jpg' if name!='M_CanopyGlass' else 'OrionCockpit.jpg',reason='Real photographed display bezels/covered optics and clear helmet visor support optical reference only. Preserve live Japanese UI, lights, and current neutral clear glass; printing photo pixels would freeze UI or occlude view.')
    else:raise ValueError('Unmapped new material family: '+name)
    bindings.append(b)
recipe=dict(status='LOCAL_SOURCE_CANDIDATE_NOT_RUNTIME_PASS',version=2,material_bindings=bindings,map_sets=sets,glass_invariant=dict(base_color_linear=[0,0,0],transmission=.995,opacity=0,two_sided=False,neutral=True),photo_bindings=[dict(id=n,photo=f,quad_px=[[b[0],b[1]],[b[2]-1,b[1]],[b[2]-1,b[3]-1],[b[0],b[3]-1]],role='reference_only',mesh_projection=False) for n,f,b in refs],uv_rule='Preserve existing UVMap. Root must validate physical UV density per material face. Use actor-local coordinates/world-aligned texture with object origin subtraction for isotropic metal only. Fabric needs aligned UV; do not triplanar normal weave. No solar system float coordinates.',review=dict(fixed_runtime='outputs/Star-Win64/Screenshots/cockpit.png',required_views=['same cockpit forward camera and exposure','seat 20cm oblique close-up','stick grip 15cm close-up','instrument metal trim 10cm with raking light','exterior ceramic/radiator front and edge','holdout cockpit side view'],current='No new UE renders: root is exclusive UE writer'))
for binding in recipe['photo_bindings']:
    if binding['id'] in ['FoilFold','BlackShield']:
        binding['role']='photo_reference_and_roughness_estimate_source'
        binding['derived_map_set']='FoilResponse' if binding['id']=='FoilFold' else 'CarbonShield'
        binding['normal_from_rgb']=False
        actual=next(s for s in sets if s['id']==binding['derived_map_set'])['crop_px']
        binding['quad_px']=[[actual[0],actual[1]],[actual[2]-1,actual[1]],[actual[2]-1,actual[3]-1],[actual[0],actual[3]-1]]
(ROOT/'Data/photo_ship_bindings.json').write_text(json.dumps(recipe,indent=2)+'\n',encoding='utf-8')
tiles=[]
for s in sets:
    for suffix in ['PhotoCrop','Modulation','Roughness','NormalDX']:
        im=Image.open(MAP/(s['id']+'_'+suffix+'.png')).convert('RGB');im.thumbnail((256,256));tiles.append((s['id']+' / '+suffix,im))
board=Image.new('RGB',(1024,len(sets)*286),'#20252b');d=ImageDraw.Draw(board)
for i,(label,im) in enumerate(tiles):x=(i%4)*256;y=(i//4)*286;board.paste(im,(x,y+25));d.text((x+5,y+5),label,fill='white')
board.save(HERE/'maps_review.png')
records=[]
for p in sorted(OUT.rglob('*')):
    if p.is_file():
        im=Image.open(p);im.verify()
        records.append(dict(file=p.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size,size=list(Image.open(p).size)))
(HERE/'checksums.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(images=len(records),photo_mapped_materials=len(assigned),total_materials=len(bindings),bytes=sum(r['bytes'] for r in records))))
