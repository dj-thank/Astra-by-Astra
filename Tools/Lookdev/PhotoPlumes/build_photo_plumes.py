"""Deterministic photo extraction; no generated noise or learned upscaling.
Run from any directory. Outputs are candidate source textures, not UE assets.
"""
from pathlib import Path
import hashlib, json, math
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
ART = ROOT / 'Content/Star/Art/PhotoPlumes'
HERE = Path(__file__).resolve().parent

def smooth(a, b, x):
    t = np.clip((x-a)/(b-a), 0, 1)
    return t*t*(3-2*t)

def main():
    source = Image.open(ART/'Source/shuttle.jpg').convert('RGB')
    # Hand-registered left SSME plume, original 2008x3000 photo pixels.
    # Nozzle start and image-bottom truncation are different boundaries.
    quad = (615,2275, 382,2999, 584,2999, 721,2302) # PIL TL,BL,BR,TR
    crop = source.transform((256,1024), Image.Transform.QUAD, quad, Image.Resampling.BICUBIC)
    rgb = np.asarray(crop,dtype=float)/255
    rgb = np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
    # Estimate dark sky / ambient glow from both border strips independently per row.
    bg = np.minimum(np.median(rgb[:,:10],axis=1),np.median(rgb[:,-10:],axis=1))
    light = np.maximum(rgb-bg[:,None,:],0)
    y,x=np.mgrid[0:1024,0:256]; u=x/255; v=y/1023
    edge=smooth(0,.075,u)*(1-smooth(.925,1,u))
    # Image is truncated; the last 18% taper is explicitly artistic, not measured.
    taper=smooth(0,.035,v)*(1-smooth(.82,1,v))
    light*= (edge*taper)[...,None]
    intensity=light.max(axis=2)
    intensity/=max(float(np.percentile(intensity,99.7)),1e-8)
    intensity=np.clip(intensity,0,1)
    chroma=light/np.maximum(light.max(axis=2,keepdims=True),1e-8)
    # RGB is linear normalized emission color; A is relative uncalibrated emission.
    rgba=np.concatenate([chroma,intensity[...,None]],axis=2)
    Image.fromarray(np.uint8(np.clip(rgba,0,1)*255)).save(ART/'T_PlumePhoto.png')
    # Axisymmetric approximation: observed transverse integral per axial row.
    # A single photograph cannot recover true 3D emissivity (no tomography claim).
    profile=np.stack([intensity.mean(axis=1),intensity.max(axis=1)],axis=1)
    profile/=np.maximum(profile.max(axis=0,keepdims=True),1e-8)
    axial=np.zeros((1024,16,4)); axial[:,:,0]=profile[:,0,None]; axial[:,:,1]=profile[:,1,None]; axial[:,:,2]=profile[:,0,None]; axial[:,:,3]=1
    Image.fromarray(np.uint8(axial*255)).save(ART/'T_PlumeAxial.png')
    crop.save(HERE/'registered_reference.png')
    # Fixed exposure CPU preview of the exact photo radiance/opacity composition.
    # This is a material signal check, not a camera render of integrated geometry.
    panel=Image.new('RGB',(1056,1100),(7,9,15)); draw=ImageDraw.Draw(panel)
    panel.paste(crop,(0,60)); draw.text((8,12),'Registered NASA photo',fill='white')
    for col,thrust in enumerate((.18,.55,1.0),1):
        length=.18+.82*math.sqrt(thrust)
        vv=v/max(length,.001)
        iy=np.minimum((vv*1023).astype(int),1023)
        sampled=rgba[iy,x]
        fade=1-smooth(.94,1,vv)
        # Same shade in Thruster.ush photo branch, phase=0, layer energy=1.
        emission=sampled[:,:,:3]*(.9+.1*np.array([.25,.65,1]))
        signal=emission*sampled[:,:,3,None]*fade[...,None]*thrust*(.55+.45*thrust)
        display=np.power(1-np.exp(-signal*3),1/2.2)
        panel.paste(Image.fromarray(np.uint8(display*255)),(col*264,60))
        draw.text((col*264+8,12),f'CPU signal / spool {thrust}',fill='white')
    panel.save(HERE/'comparison.png')
    # Swept intersecting radial sheets: 6 full planes, 24 axial subdivisions.
    # Unit nozzle diameter D. +X is exhaust direction in this local asset only.
    # Root maps this onto socket -X (UE vessel +X forward).
    vertices=[]; uv=[]; faces=[]
    for plane in range(6):
        angle=math.pi*plane/6
        base=len(vertices)
        for row in range(25):
            t=row/24
            radius=.5*(1+.35*t)*(1-.80*smooth(.80,1,t))
            for side in (-1,1):
                vertices.append([8*t,side*radius*math.cos(angle),side*radius*math.sin(angle)])
                uv.append([(side+1)/2,t])
        for row in range(24):
            a=base+row*2
            faces.extend([[a,a+2,a+1],[a+1,a+2,a+3]])
    mesh={'units':'nozzle_diameter_D','local_exhaust_axis':'+X','vertices':vertices,'uv0':uv,'triangles':faces,'interpretation':'swept emissive sheets; inferred azimuthal structure, not observed 3D'}
    (ART/'plume_mesh.json').write_text(json.dumps(mesh,separators=(',',':')),encoding='utf-8')
    manifest={'schema':1,'source':{'id':'KSC-08pd0715','url':'https://images-assets.nasa.gov/image/KSC-08pd0715/KSC-08pd0715~orig.jpg','page':'https://images.nasa.gov/details/KSC-08pd0715','date':'2008-03-11','credit':'NASA/Jerry Cannon, Rusty Backer','license':'NASA media usage guidelines; US government work; no endorsement','policy':'https://www.nasa.gov/nasa-brand-center/images-and-media/','original_size_px':list(source.size),'sha256':hashlib.sha256((ART/'Source/shuttle.jpg').read_bytes()).hexdigest(),'original_color_space':'sRGB assumed from JPEG','observation':'STS-123 atmospheric launch, left SSME exhaust'},'binding':{'quad_px_TL_BL_BR_TR':list(quad),'uv':'U transverse; V nozzle to image bottom','sampled_length_in_nozzle_diameters_estimate':8,'scale_status':'projected ratio only; no camera calibration; D supplied by actual socket geometry','source_nozzle_width_px_estimate':105},'interpretations':['last 18 percent taper beyond photographic truncation','6 azimuthal sheet repetitions','8 D runtime extent','cruise stretch and spool response','spacecraft vacuum luminous exhaust; atmospheric cells remain cinematic reference'],'textures':{'T_PlumePhoto.png':{'size':[256,1024],'srgb':False,'channels':'RGB normalized linear emission chroma; A relative emission intensity; straight alpha','address':'clamp','mips':True},'T_PlumeAxial.png':{'size':[16,1024],'srgb':False,'channels':'R/B transverse mean; G transverse max; A 1','address':'clamp','mips':True}},'not_observed':['absolute radiance or plasma temperature','vacuum shock cells','3D density','extra temporal frames'],'files':{}}
    for p in (ART/'T_PlumePhoto.png',ART/'T_PlumeAxial.png',ART/'plume_mesh.json'):
        manifest['files'][p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
    (ROOT/'Data/photo_plumes_sources.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    assert np.isfinite(rgba).all() and intensity.max()>0.9
    assert np.count_nonzero(intensity[-1])==0
    assert len(faces)==288 and len(vertices)==300
    print('PASS: photo extraction, finite signals, zero tip, 300 vertices / 288 triangles. GPU NOT RUN.')

if __name__=='__main__': main()
