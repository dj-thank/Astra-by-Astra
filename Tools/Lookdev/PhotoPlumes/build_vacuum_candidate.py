"""Actual in-orbit Draco plume extraction; preserves atmospheric candidate.
Same linear straight-alpha texture contract; no added noise or shock diamonds.
"""
from pathlib import Path
import json,hashlib,math
import numpy as np
from PIL import Image,ImageDraw
from preview_geometry import render,smooth

ROOT=Path(__file__).resolve().parents[3]
ART=ROOT/'Content/Star/Art/PhotoPlumes'
HERE=Path(__file__).resolve().parent

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    source=ART/'Source/iss066e125277_original.jpg'
    oms=ART/'Source/engine_sts51_big.jpg'
    assert sha(source)=='d47ffb82752ddce10c11c15305662aa76585a20d4f3c25bc8e8ce351c454dda0'
    assert sha(oms)=='e0246fea390105994de4776a822817da33dc04b9f0362fc098edf9dcca855daf'
    photo=Image.open(source).convert('RGB')
    # Observed jet left of Dragon body; exclude ship and station foreground.
    # PIL order TL BL BR TR. Nozzle itself is hidden by spacecraft body.
    quad=tuple(value*5568/1041 for value in (337,305,165,400,205,458,359,335))
    crop=photo.transform((256,512),Image.Transform.QUAD,quad,Image.Resampling.BICUBIC)
    rgb=np.asarray(crop,dtype=float)/255
    rgb=np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
    # Black-space subtraction, then cautious exposure normalization.
    bg=np.minimum(np.median(rgb[:,:10],axis=1),np.median(rgb[:,-10:],axis=1))
    light=np.maximum(rgb-bg[:,None,:]-.0003,0)
    y,x=np.mgrid[:512,:256]; u=x/255; v=y/511
    fade=smooth(0,.04,v)*(1-smooth(.70,1,v))*smooth(0,.10,u)*(1-smooth(.9,1,u))
    light*=fade[...,None]
    intensity=light.max(axis=2)
    reference=float(np.percentile(intensity,99.5))
    intensity=np.clip(intensity/max(reference,1e-8),0,1)
    chroma=light/np.maximum(light.max(axis=2,keepdims=True),1e-8)
    rgba=np.concatenate([chroma,intensity[...,None]],axis=2)
    Image.fromarray(np.uint8(rgba*255)).save(ART/'T_VacuumPlumePhoto.png')
    profile=np.stack([intensity.mean(axis=1),intensity.max(axis=1)],axis=1)
    profile/=np.maximum(profile.max(axis=0,keepdims=True),1e-8)
    axial=np.zeros((512,16,4)); axial[:,:,0]=profile[:,0,None]; axial[:,:,1]=profile[:,1,None]; axial[:,:,2]=profile[:,0,None]; axial[:,:,3]=1
    Image.fromarray(np.uint8(axial*255)).save(ART/'T_VacuumPlumeAxial.png')
    # Preserve topology/API, use divergent chemical jet envelope. Engineering
    # dimensions belong to fictional nozzle D, not metrology of this photograph.
    mesh=json.loads((ART/'plume_mesh.json').read_text(encoding='utf-8'))
    for i,uv in enumerate(mesh['uv0']):
        t=uv[1]; plane=i//50; side=-1 if i%2==0 else 1
        radius=.5+1.25*t
        a=math.pi*plane/6
        mesh['vertices'][i]=[6*t,side*radius*math.cos(a),side*radius*math.sin(a)]
    mesh['interpretation']='inferred expanding vacuum chemical jet; 6D long; 3.5D max width; actual Draco photo texture; no calibrated 3D density'
    (ART/'vacuum_plume_mesh.json').write_text(json.dumps(mesh,separators=(',',':')),encoding='utf-8')
    disk=json.loads((ART/'plume_disks.json').read_text(encoding='utf-8'))
    for i,uv in enumerate(disk['uv0']):
        r,t=uv; j=i%17
        a=2*math.pi*(j-1)/16 if j else 0
        radius=(.5+1.25*t)*r
        disk['vertices'][i]=[6*t,radius*math.cos(a),radius*math.sin(a)]
    disk['interpretation']='inferred divergent vacuum jet transverse slices; real photo mean intensity'
    (ART/'vacuum_plume_disks.json').write_text(json.dumps(disk,separators=(',',':')),encoding='utf-8')
    panel=Image.new('RGB',(1064,650),(6,8,12)); d=ImageDraw.Draw(panel)
    panel.paste(crop,(0,60)); d.text((8,12),'Actual Draco orbital jet crop',fill='white')
    d.text((8,590),'Approx. 960 source px axial. Native 5568x3712 image.',fill='white')
    old=np.asarray(Image.open(ART/'T_PlumePhoto.png').resize((256,512)),dtype=float)/255
    for col,data in enumerate((old,rgba),1):
        signal=data[:,:,:3]*data[:,:,3,None]
        display=np.power(1-np.exp(-signal*3),1/2.2)
        panel.paste(Image.fromarray(np.uint8(display*255)),(col*264,60))
        d.text((col*264+8,12),'Atmospheric reference' if col==1 else 'Vacuum candidate (normalized)',fill='white')
    ref=Image.open(oms).convert('RGB'); ref.thumbnail((264,512))
    panel.paste(ref,(792,60)); d.text((796,12),'Discovery OMS orbital reference',fill='white')
    d.text((796,280),'Reference only; not a texture.',fill='white')
    panel.save(HERE/'vacuum_comparison.png')
    views=Image.new('RGB',(800,1240),(6,8,12)); d=ImageDraw.Draw(views)
    for i,angle in enumerate((0,45,80,90)):
        signal=render(mesh,angle,rgba)+render(disk,angle,axial,True)
        assert np.isfinite(signal).all() and signal.max()>0
        display=np.power(1-np.exp(-signal*3),1/2.2)
        views.paste(Image.fromarray(np.uint8(display*255)),(0,i*310+30))
        d.text((8,i*310+8),f'Vacuum CPU geometry / {angle} deg / normalized exposure / not UE',fill='white')
    views.save(HERE/'vacuum_geometry_views.png')
    metadata={'schema':1,'preferred_runtime_candidate':'vacuum chemical','sources':[{'id':'iss066e125277','file':str(source.relative_to(ROOT)).replace('\\','/'),'sha256':sha(source),'url':'https://images-assets.nasa.gov/image/iss066e125277/iss066e125277~orig.jpg','page':'https://www.nasa.gov/image-article/plumes-from-spacex-cargo-dragons-draco-engines/','observation_date':'2022-01-23','credit':'NASA','license':'NASA US government media; no endorsement','observed':'Cargo Dragon Draco firings after ISS undocking','native_size':list(photo.size)},{'id':'STS-51-OMS','file':str(oms.relative_to(ROOT)).replace('\\','/'),'sha256':sha(oms),'url':'https://apod.nasa.gov/apod/image/9803/engine_sts51_big.jpg','page':'https://apod.nasa.gov/apod/ap980308.html','observation_date':'STS-51, 1993-09; exact frame time unknown','publication_date':'1998-03-08','credit':'NASA, STS-51 Crew','license':'NASA US government media; no endorsement','usage':'visual reference only; not sampled into textures'}],'binding':{'quad_TL_BL_BR_TR_px':list(quad),'source_axial_pixels_approx':960,'texture_dimensions':[256,512],'nozzle_occluded':True,'color_space':'source sRGB assumed; derived RGBA linear','intensity_units':'relative photographic exposure; not radiance','normalization_percentile':99.5,'normalization_linear_value':reference},'observed':['white faint smooth divergent orbital chemical plume','no repeated atmospheric shock diamonds in sampled jet'],'authored':['nozzle-relative 6D length and 3.5D max width','azimuthal repetition and cross-section reconstruction','edge/background subtraction; normalization; tail taper','spool-length and temporal pulse; not time-resolved source evidence'],'not_claimed':['calibrated brightness','volume density reconstruction','realistic electric ion engine exhaust','photographically measured fictional engine dimensions'],'runtime':{'texture':'T_VacuumPlumePhoto.png','axial_texture':'T_VacuumPlumeAxial.png','mesh':'vacuum_plume_mesh.json','disks':'vacuum_plume_disks.json','api_change':False,'prefer_main_two':True,'RCS_recipe':'same photo with shorter 2D to 4D axis scale, individual pulse timing, no ambient smoke','brightness_recipe':'start main radiance 40, RCS 8 with existing LayerEnergy; artistic UE exposure starting values only; do not retain 800 by default'},'validation':{'cpu_geometry_angles':[0,45,80,90],'source_hashes':'PASS','native_resolution_limit':'960px jet; downsampled to 512px; no synthetic detail added','gpu':'NOT_RUN','residuals':['oblique plane lobes need UE occlusion/angle blending review','photo uses one exposure, not measured light output','static texture cannot reproduce real puff evolution']},'files':{}}
    for name in ('T_VacuumPlumePhoto.png','T_VacuumPlumeAxial.png','vacuum_plume_mesh.json','vacuum_plume_disks.json'):
        p=ART/name; metadata['files'][name]={'sha256':sha(p),'bytes':p.stat().st_size}
    (ROOT/'Data/photo_plumes_vacuum.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    assert np.count_nonzero(intensity[-1])==0 and np.isfinite(rgba).all()
    print('PASS vacuum source hashes, finite RGBA, 4 angle CPU geometry. Old candidate preserved. UE NOT RUN.')

if __name__=='__main__': main()
