"""Build bounded world-space plume slices and compare CPU material signals.

No UE renderer, bloom, depth buffer, physical radiance or fluid simulation claim.
Photo pixels are read unchanged. Geometry, tint and exposure floor are authored.
"""
from pathlib import Path
import hashlib
import json
import math
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
OLD = ROOT / 'Content/Star/Art/PhotoPlumes'
ART = ROOT / 'Content/Star/Art/EngineFXV3'
OUT = ROOT / 'work/validation/engine-fx-v3'


def smooth(a, b, x):
    t = np.clip((x-a)/(b-a), 0, 1)
    return t*t*(3-2*t)


def gain(exposure, target=6, physical=40):
    e = np.clip(exposure, 1e-6, 64)
    return np.minimum(np.maximum(physical, target/e), np.minimum(1.5*target/e, 2e6))


def tint(radial, axial):
    core = np.exp(-6*radial**2)*(1-.65*np.clip(axial, 0, 1))
    authored = np.array([.30,.52,1.0]) + core[...,None]*np.array([.70,.17,-.66])
    return 1+.75*(authored-1)


def shader(q, tex, disk, v3, exposure, spool, weight=1):
    # Old runtime puts view weight into spool, which incorrectly changes length.
    s = spool if v3 else spool*weight
    axial = q[...,1]/(.18+.82*np.sqrt(s))
    ix = np.clip((q[...,0]*(tex.shape[1]-1)).astype(int),0,tex.shape[1]-1)
    iy = np.clip((np.clip(axial,0,1)*(tex.shape[0]-1)).astype(int),0,tex.shape[0]-1)
    p = tex[iy, ix]
    tip = 1-smooth(.94,1,axial)
    common = tip*s*(.55+.45*s)
    if disk:
        radial = np.clip(q[...,0],0,1)
        profile = .65*p[...,0]+.35*p[...,1] if v3 else p[...,0]
        alpha = profile*(1-smooth(0 if v3 else .05,1,radial))*common
        color = tint(radial,axial) if v3 else np.array([.55,.7,1])
        layer = .125 if v3 else .025
    else:
        alpha = p[...,3]*common
        color = p[...,:3]*(tint(np.abs(q[...,0]*2-1),axial) if v3 else np.array([.925,.965,1]))
        layer = 1/6
    return color*alpha[...,None]*layer*(gain(exposure)*weight if v3 else 40)*exposure


def render(mesh, angle, tex, disk, v3, exposure=1e-4, spool=1):
    width,height=520,220
    theta=math.radians(angle)
    align=abs(math.sin(theta))
    blend=smooth(.34202014,.93969262,align)
    weight=blend if disk else 1-blend
    p=np.asarray(mesh['vertices']); uv=np.asarray(mesh['uv0'])
    xy=np.stack([p[:,0]*math.cos(theta)-p[:,1]*math.sin(theta),p[:,2]],axis=1)
    xy[:,0]=(xy[:,0]-3*math.cos(theta))*66+width/2
    xy[:,1]=xy[:,1]*66+height/2
    result=np.zeros((height,width,3))
    covered=np.zeros((height,width),dtype=bool)
    group_size = (32 if v3 else 16) if disk else 48
    for face_index,face in enumerate(mesh['triangles']):
        if face_index % group_size == 0: covered[:]=False
        pts=xy[face]; coords=uv[face]
        lo=np.maximum(np.floor(pts.min(axis=0)).astype(int),0)
        hi=np.minimum(np.ceil(pts.max(axis=0)).astype(int),[width-1,height-1])
        if (hi<lo).any(): continue
        a,b,c=pts
        den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-8: continue
        yy,xx=np.mgrid[lo[1]:hi[1]+1,lo[0]:hi[0]+1]; xx=xx+.5; yy=yy+.5
        ba=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
        bb=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
        bc=1-ba-bb
        # Half-open raster edges suppress adjacent-triangle double contribution.
        mask=(ba>=-1e-10)&(bb>=-1e-10)&(bc>=-1e-10)
        previous=covered[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
        mask &= ~previous
        previous |= mask
        q=ba[...,None]*coords[0]+bb[...,None]*coords[1]+bc[...,None]*coords[2]
        signal=shader(q,tex,disk,v3,exposure,spool,weight)
        result[lo[1]:hi[1]+1,lo[0]:hi[0]+1]+=signal*mask[...,None]
    return result


def display(signal):
    # Simple shoulder for comparison, explicitly not UE ACES recreation.
    return np.uint8(np.clip((1-np.exp(-signal))**(1/2.2)*255,0,255))


def main():
    ART.mkdir(parents=True,exist_ok=True); OUT.mkdir(parents=True,exist_ok=True)
    photo=np.asarray(Image.open(OLD/'T_VacuumPlumePhoto.png'),float)/255
    axial=np.asarray(Image.open(OLD/'T_VacuumPlumeAxial.png'),float)/255
    oldmesh=json.loads((OLD/'vacuum_plume_mesh.json').read_text(encoding='utf-8'))
    olddisk=json.loads((OLD/'vacuum_plume_disks.json').read_text(encoding='utf-8'))
    mesh=json.loads(json.dumps(oldmesh))
    # Retain exactly 50 verts / 48 triangles per plane for runtime LOD grouping.
    for i,uv in enumerate(mesh['uv0']):
        t=uv[1]; plane=i//50; side=-1 if i%2==0 else 1
        radius=.5+.65*t; a=math.pi*plane/6
        mesh['vertices'][i]=[6*t,side*radius*math.cos(a),side*radius*math.sin(a)]
    mesh['interpretation']='Authored fictional engine envelope: 6D long, 2.3D final diameter; six unchanged Draco photo sheets; not measured 3D density.'
    vertices=[]; uv=[]; triangles=[]
    for t in (.025,.075,.15,.25,.38,.53,.70,.86):
        base=len(vertices); vertices.append([6*t,0,0]); uv.append([0,t])
        for seg in range(32):
            a=math.tau*seg/32; radius=.5+.65*t
            vertices.append([6*t,radius*math.cos(a),radius*math.sin(a)]); uv.append([1,t])
        for seg in range(32): triangles.append([base,base+1+seg,base+1+(seg+1)%32])
    disk={'units':'nozzle_diameter_D','local_exhaust_axis':'+X','vertices':vertices,'uv0':uv,'triangles':triangles,
          'uv_contract':'U radial 0=center 1=edge; V axial position',
          'interpretation':'Authored 8 transverse slices, 32 sides, denser near throat. Draco mean/peak axial profile. Not tomography.'}
    for name,data in [('engine_plume_mesh.json',mesh),('engine_plume_disks.json',disk)]:
        (ART/name).write_text(json.dumps(data,separators=(',',':'))+'\n',encoding='utf-8')
        p=np.asarray(data['vertices']); indices=np.asarray(data['triangles'])
        assert np.isfinite(p).all() and indices.min()>=0 and indices.max()<len(p)
        assert len(data['uv0'])==len(p) and p[:,0].min()>=0
    assert len(mesh['triangles'])==288 and len(disk['triangles'])==256
    # Non-uniform axial slices avoid repeated shock-cell-like ring spacing.
    angles=[0,30,45,60,70,80,90]
    panel=Image.new('RGB',(1040,250*len(angles)),(5,7,11)); draw=ImageDraw.Draw(panel)
    rows=[]
    for row,angle in enumerate(angles):
        metrics={'angle_degrees_from_side':angle}
        for col,v3 in enumerate([False,True]):
            signal=render(mesh if v3 else oldmesh,angle,photo,False,v3)+render(disk if v3 else olddisk,angle,axial,True,v3)
            assert np.isfinite(signal).all() and signal.max()>0
            panel.paste(Image.fromarray(display(signal)),(col*520,row*250+28))
            draw.text((col*520+8,row*250+8),f'{"V3" if v3 else "V2"} CPU | {angle} deg | E=0.0001 | throttle=1',fill='white')
            peak=float(signal.max()); energy=float(signal.sum())
            metrics['v3' if v3 else 'v2']={'peak_exposed_linear':peak,'integrated_signal':energy}
        rows.append(metrics)
    panel.save(OUT/'angle_before_after.png')
    # Bounded exposure sweep. Includes cap regime outside expected operation.
    envelope=[]
    for e in [1e-6,1e-5,1e-4,1e-3,.01,.1,1,16,64]:
        g=float(gain(e)); exposed=g*e
        assert math.isfinite(g) and 0<=g<=2e6 and exposed<=9.000001
        if e>=3e-6: assert exposed>=5.999999
        envelope.append({'eye_exposure':e,'v2_exposed_gain':40*e,'v3_scene_gain':g,'v3_exposed_gain':exposed})
    q=np.stack(np.meshgrid(np.linspace(0,1,41),np.linspace(0,1,81)),axis=-1)
    for e in [1e-6,1e-4,1,64]:
        for d,tex in [(False,photo),(True,axial)]:
            assert np.count_nonzero(shader(q,tex,d,True,e,0))==0
            for spool in [.01,.05,.25,.5,1]:
                signal=shader(q,tex,d,True,e,spool)
                assert np.isfinite(signal).all() and signal.max()>0
            assert np.count_nonzero(shader(q,tex,d,True,e,1,0))==0
    peaks=[x['v3']['peak_exposed_linear'] for x in rows]
    assert min(peaks)>.1 and max(peaks)<9
    # Source comparison uses original crop alongside existing linear photo.
    meta=json.loads((ROOT/'Data/photo_plumes_vacuum.json').read_text(encoding='utf-8'))
    source=ROOT/meta['sources'][0]['file']
    assert hashlib.sha256(source.read_bytes()).hexdigest()==meta['sources'][0]['sha256']
    crop=Image.open(source).convert('RGB').transform((256,512),Image.Transform.QUAD,
          tuple(meta['binding']['quad_TL_BL_BR_TR_px']),Image.Resampling.BICUBIC)
    reference=Image.new('RGB',(768,550),(5,7,11)); rd=ImageDraw.Draw(reference)
    reference.paste(crop,(0,30))
    reference.paste(Image.fromarray(display(photo[:,:,:3]*photo[:,:,3,None]*3)),(256,30))
    y,x=np.mgrid[:512,:256]; qp=np.stack([x/255,y/511],axis=-1)
    reference.paste(Image.fromarray(display(shader(qp,photo,False,True,1e-4,1)*6)),(512,30))
    for i,label in enumerate(['NASA orbital crop','Unchanged linear extraction','Authored V3 color/exposure']): rd.text((i*256+4,8),label,fill='white')
    reference.save(OUT/'source_comparison.png')
    exposure_panel=Image.new('RGB',(1040,560),(5,7,11)); ed=ImageDraw.Draw(exposure_panel)
    for col,e in enumerate([1e-5,1e-4,.01,1]):
        for row,v3 in enumerate([False,True]):
            signal=shader(qp,photo,False,v3,e,1)*6
            tile=Image.fromarray(display(signal)).resize((256,256))
            exposure_panel.paste(tile,(260*col,280*row+24))
            ed.text((260*col+4,280*row+5),f'{"V3" if v3 else "V2"} | E={e:g} | no bloom',fill='white')
    exposure_panel.save(OUT/'exposure_before_after.png')
    report={'schema':1,'scope':'CPU analytic signal and orthographic slice raster only; no UE/GPU/physical input acceptance',
            'angle_measurements':rows,'exposure_envelope':envelope,
            'tests':{'off_exact_zero':'PASS','finite_and_bounded_emission':'PASS','mesh_indices_units':'PASS',
                     'source_hash':'PASS','lit_all_7_angles':'PASS','UE_shader_compile':'NOT_RUN','depth_occlusion':'NOT_RUN','gpu_time':'NOT_RUN'},
            'known_limits':['CPU raster uses nearest texture sample; UE uses filtered mipmaps','No bloom, perspective, depth occlusion or UE tonemapper in CPU panels',
                            'Integrated angle energy is not constant because projected physical area changes; inspect peaks as well',
                            'Low LOD and exposure interpolation require GPU checks','Extreme E<3e-6 falls below target by scene emission cap']}
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    manifest={'schema':1,'source_manifest':'Data/photo_plumes_vacuum.json','source_texture':'Content/Star/Art/PhotoPlumes/T_VacuumPlumePhoto.png',
              'source_axial':'Content/Star/Art/PhotoPlumes/T_VacuumPlumeAxial.png','photo_pixels_changed':False,
              'authored':['warm core / cool halo color at FictionalTint=.75','bounded exposure-relative emission','6D length and 2.3D width geometry','azimuthal repetition and axial slices'],
              'observation_not_claimed':['shock diamonds in vacuum','calibrated radiance','measured volumetric flow','photoreal GPU acceptance'],
              'material':{'define':'STAR_ENGINE_FX_V3=1','EyeExposure':'MaterialExpressionEyeAdaptation','DisplayRadiance_main':6,'DisplayRadiance_RCS':1.2,
                          'FictionalTint':.75,'LayerEnergy_plane':1/6,'LayerEnergy_axial':.125,'LayerEnergy_2plane_LOD':.5,
                          'ThrusterRadiance_main':40,'ThrusterRadiance_RCS':8,'Thrust':'raw shared spool; do not multiply by view weight','ViewAngleWeight':'separate opacity weight'},
              'geometry':{'axis':'+X local exhaust, explicitly rotate to nozzle axis','units':'multiply coordinates by nozzle diameter in centimeters',
                          'planes':'Content/Star/Art/EngineFXV3/engine_plume_mesh.json','disks':'Content/Star/Art/EngineFXV3/engine_plume_disks.json',
                          'triangles_per_emitter':544,'screen_billboard':False},'validation':report['tests']}
    (ROOT/'Data/engine_fx_v3.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'tests':report['tests'],'v3_angle_peaks':peaks,'output':str(OUT)},indent=2))


if __name__=='__main__':
    main()
