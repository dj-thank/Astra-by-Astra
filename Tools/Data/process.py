"""Deterministic scientific-data conversion; no generated replacement imagery."""
from __future__ import annotations
import io
import json
import math
import shutil
import struct
from pathlib import Path
import numpy as np
from PIL import Image
import png
import tifffile
import OpenEXR
from scipy.interpolate import LinearNDInterpolator
from acquire import ROOT, RAW, digest, save_json

TEXTURES=ROOT/'Content/Star/Textures'
DATA=ROOT/'Content/Star/Data'
Image.MAX_IMAGE_PIXELS=500_000_000
MANIFEST=[]

def artifact(path, source_ids, **metadata):
    catalog=json.loads((ROOT/'Tools/Data/sources.json').read_text(encoding='utf-8'))
    sources=[]
    for id in source_ids:
        item=next(x for x in catalog if x['id']==id)
        receipt=json.loads((RAW/(item['filename']+'.receipt.json')).read_text(encoding='utf-8'))
        sources.append(receipt)
    row=dict(path=path.relative_to(ROOT).as_posix(),sha256=digest(path),bytes=path.stat().st_size,
             sources=sources,**metadata)
    MANIFEST.append(row)
    print('PROCESSED',path.name,path.stat().st_size,flush=True)
    return row

def srgb_to_linear(x):
    return np.where(x<=0.04045,x/12.92,((x+0.055)/1.055)**2.4)

def linear_to_srgb(x):
    x=np.clip(x,0,1)
    return np.where(x<=0.0031308,x*12.92,1.055*x**(1/2.4)-0.055)

def resize_srgb(source, target, size):
    # One linear-light channel at a time bounds peak allocation below 2GB for 21.6k input.
    image=Image.open(source).convert('RGB'); out=np.empty((size[1],size[0],3),dtype=np.uint8)
    for c in range(3):
        channel=np.asarray(image.getchannel(c),dtype=np.float32)/255
        linear=srgb_to_linear(channel)
        res=np.asarray(Image.fromarray(linear).resize(size,Image.Resampling.LANCZOS))
        out[:,:,c]=np.rint(linear_to_srgb(res)*255).astype(np.uint8)
    Image.fromarray(out).save(target,quality=96,subsampling=0,optimize=False)

def moon():
    rgb=tifffile.imread(RAW/'lroc_color_16bit_srgb_8k.tif')
    assert rgb.shape==(4096,8192,3) and rgb.dtype==np.uint16
    p=TEXTURES/'moon_albedo_8k.png'
    with p.open('wb') as f:
        png.Writer(width=8192,height=4096,greyscale=False,bitdepth=16,compression=6).write(f,rgb.reshape(4096,-1))
    artifact(p,['moon_albedo'],observationDate='LROC composite; visual map produced December 2025',
             units='normalized display RGB',datum='Moon mean Earth/polar axis; plate carree centered 0 longitude',
             colorSpace='sRGB encoded; UE sRGB=true; PNG RGB16',dimensions=[8192,4096],
             process='Lossless TIFF uint16 RGB to PNG uint16 RGB. No gamma transform or synthetic detail.',
             scientificLimit='NASA aesthetic adaptation, white balance/exposure adjustment and polar inpainting; not raw scientific albedo.',
             credit='NASA Scientific Visualization Studio; Ernie Wright; NASA/GSFC/Arizona State University; LRO LROC/LOLA')
    raw=tifffile.imread(RAW/'ldem_16_uint.tif')
    assert raw.shape==(2880,5760) and raw.dtype==np.uint16
    decoded=raw.astype(np.int32)-20000
    assert decoded.min()>=-32768 and decoded.max()<=32767
    p=DATA/'moon_ldem_16_i16.bin'; decoded.astype('<i2').tofile(p)
    meta=dict(schemaVersion=1,binaryPath=p.relative_to(ROOT).as_posix(),width=5760,height=2880,
              format='int16',endianness='little',layout='row-major',headerBytes=0,
              scaleMeters=0.5,offsetMeters=0,referenceRadiusMeters=1737400,
              longitudeConvention='east-positive; [-180,180)',latitudeConvention='planetocentric',
              rowOrder='north-to-south',columnOrder='west-to-east',pixelRegistration='area; sample at pixel center',
              longitudeAtColumnCenter='-180 + (column + 0.5) * 360 / width',
              latitudeAtRowCenter='90 - (row + 0.5) * 180 / height',
              sampleCoordinates='x=(longitude+180)/360*width-0.5; y=(90-latitude)/180*height-0.5',
              sampling='bilinear; X wraps; Y clamps to first/last sample; no missing pixels',
              sourceDecoding='source uint16 sample U: heightMeters=(int32(U)-20000)*0.5; radialMeters=1737400+heightMeters',
              minHeightMeters=float(decoded.min()*0.5),maxHeightMeters=float(decoded.max()*0.5),
              datumFrame='MOON_ME (mean Earth/polar axis)',nominalEquatorialPostSpacingMeters=1737400*math.pi/180/16)
    save_json(DATA/'moon_ldem_16.json',meta)
    artifact(p,['moon_dem'],observationDate='LOLA gridded data as of spring 2019',units='signed half meters above 1737400m sphere',
             datum='Moon ME, planetocentric east longitude',colorSpace='not applicable; numeric signed int16',
             process='Subtract 20000 in signed int32 then cast little-endian int16 without resampling.',
             credit='NASA/GSFC/MIT; LRO LOLA; NASA Scientific Visualization Studio')
    artifact(DATA/'moon_ldem_16.json',['moon_dem'],observationDate='spring 2019',units='meters/degrees',datum='Moon ME',colorSpace='not applicable',
             process='Explicit lossless decoding/georeferencing metadata.',credit='NASA/GSFC/MIT; LRO LOLA')

def earth_stars():
    for id,filename,target,date,credit in [
        ('earth_day','world.200409.3x21600x10800.jpg','earth_day_8k.jpg','September 2004','NASA Earth Observatory; Reto Stockli; MODIS science team'),
        ('earth_night','dnb_land_ocean_ice.2012.13500x6750.jpg','earth_night_8k.jpg','April and October 2012','NASA Earth Observatory; NOAA NGDC; Suomi NPP VIIRS')]:
        p=TEXTURES/target;resize_srgb(RAW/filename,p,(8192,4096))
        artifact(p,[id],observationDate=date,units='display RGB, not radiance',datum='Earth geographic plate carree centered 0 longitude; north at top',
                 colorSpace='assumed sRGB display encoding; resized in linear light; UE sRGB=true',dimensions=[8192,4096],
                 process='sRGB decode, per-channel Lanczos resize to 8192x4096, sRGB encode, JPEG quality96 no chroma subsampling.',
                 scientificLimit='Historical cloud-free visual composite. Night image includes dim land/ocean/ice background; not a pure city-emission mask.',credit=credit)
    p=TEXTURES/'stars_icrf_j2000_8k.exr';shutil.copyfile(RAW/'starmap_2020_8k.exr',p)
    with OpenEXR.File(str(p),header_only=True) as f:
        dw=f.header()['dataWindow'];assert tuple(dw[1]-dw[0]+1)==(8192,4096)
    artifact(p,['stars'],observationDate='catalog composite released 2020-09-09',units='relative HDR linear RGB',
             datum='ICRF/J2000 equatorial celestial coordinates; RA increases left; 0h at image center',colorSpace='linear half-float OpenEXR; UE sRGB=false',
             dimensions=[8192,4096],process='Byte-preserved EXR; no tone mapping.',
             credit='NASA Scientific Visualization Studio; Ernie Wright; Hipparcos-2, Tycho-2, Gaia DR2 and supplementary catalogues',
             scientificLimit='All bright stars already included. Do not add a separate bright-star layer on top.')
    save_json(DATA/'sky_mapping.json',dict(schemaVersion=1,texturePath=p.relative_to(ROOT).as_posix(),
        inputBasis='ecliptic-j2000',textureBasis='icrf-j2000-equatorial',obliquityDegrees=23.439291111111,
        directionEclipticToEquatorialRowMajor=[[1,0,0],[0,math.cos(math.radians(23.439291111111)),-math.sin(math.radians(23.439291111111))],
                                              [0,math.sin(math.radians(23.439291111111)),math.cos(math.radians(23.439291111111))]],
        uv='ra=atan2(equatorialY,equatorialX); dec=asin(equatorialZ); u=fract(0.5-ra/(2*pi)); v=0.5-dec/pi',
        note='Directions are unit vectors, no positional parallax; all bright stars included.'))

def glb_data():
    raw=(RAW/'Saturn_1_120536.glb').read_bytes();n,t=struct.unpack_from('<II',raw,12)
    assert raw[:4]==b'glTF' and t==0x4E4F534A
    g=json.loads(raw[20:20+n]);off=20+n;length,kind=struct.unpack_from('<II',raw,off)
    return g,raw[off+8:off+8+length]

def accessor(g,b,i):
    a=g['accessors'][i];v=g['bufferViews'][a['bufferView']]
    count={'VEC2':2,'VEC3':3,'SCALAR':1}[a['type']]
    return np.frombuffer(b,dtype={5126:'<f4',5123:'<u2'}[a['componentType']],count=a['count']*count,
                         offset=v.get('byteOffset',0)+a.get('byteOffset',0)).reshape(a['count'],count)

def saturn():
    g,b=glb_data();images=[]
    for image in g['images']:
        v=g['bufferViews'][image['bufferView']];start=v.get('byteOffset',0)
        images.append(Image.open(io.BytesIO(b[start:start+v['byteLength']])).convert('RGBA'))
    # NASA's body image is a cube atlas, NOT equirectangular. Reproject using its actual mesh UVs.
    positions=accessor(g,b,0).astype(float);positions[:,1]/=451.019/500.0
    uv=accessor(g,b,2).astype(float)
    atlas=np.asarray(images[0]);width,height=4096,2048
    result=np.zeros((height,width,3),dtype=np.uint8)
    lon=(np.arange(width)+0.5)/width*(2*np.pi)-np.pi
    for start in range(0,height,128):
        lat=np.pi/2-(np.arange(start,min(start+128,height))+0.5)/height*np.pi
        x=np.cos(lat)[:,None]*np.cos(lon);z=np.cos(lat)[:,None]*np.sin(lon)
        y=np.broadcast_to(np.sin(lat)[:,None],x.shape);rays=np.stack([x,y,z],axis=-1)
        major=np.argmax(np.abs(rays),axis=-1);pixels=np.empty((*x.shape,2))
        for axis in range(3):
            others=[i for i in range(3) if i!=axis]
            for sign in (-1,1):
                mask=(major==axis)&(rays[:,:,axis]*sign>0)
                # Atlas has duplicated seam vertices; select one consistent texture face region by face-center UV.
                dom=np.max(np.abs(positions),axis=1)
                selected=(positions[:,axis]*sign>=dom-0.04)
                pts=positions[selected];uvs=uv[selected]
                # Cube-atlas seam duplicates can belong to adjacent face. Find the majority atlas cell for the interior.
                cells=np.floor(uvs*np.array([4,3])).astype(int);cells=np.clip(cells,[0,0],[3,2])
                inner=np.abs(pts[:,others]).max(axis=1)<np.abs(pts[:,axis])*0.999
                cell=np.unique(cells[inner],axis=0,return_counts=True);best=cell[0][np.argmax(cell[1])]
                # Keep border vertices that lie in this cell inclusive with tolerance.
                lower=best/np.array([4,3]);upper=(best+1)/np.array([4,3])
                use=np.all((uvs>=lower-0.001)&(uvs<=upper+0.001),axis=1)
                pts=pts[use];uvs=uvs[use];denom=np.abs(pts[:,axis])
                points=pts[:,others]/denom[:,None]
                values=np.column_stack([uvs/denom[:,None],1/denom])
                interpolation=LinearNDInterpolator(points,values)
                target=rays[mask][:,others]/np.abs(rays[mask][:,axis])[:,None]
                sample=interpolation(target)
                if not np.isfinite(sample).all():raise RuntimeError('Cube-atlas projection has uncovered pixels')
                pixels[mask]=sample[:,:2]/sample[:,2,None]
        # glTF UV v=0 is image top. Bilinear image sampling, no invented polar fill.
        tx=np.clip(pixels[:,:,0]*atlas.shape[1]-0.5,0,atlas.shape[1]-1)
        ty=np.clip(pixels[:,:,1]*atlas.shape[0]-0.5,0,atlas.shape[0]-1)
        x0=tx.astype(int);y0=ty.astype(int);x1=np.minimum(x0+1,atlas.shape[1]-1);y1=np.minimum(y0+1,atlas.shape[0]-1)
        fx=(tx-x0)[:,:,None];fy=(ty-y0)[:,:,None]
        result[start:start+len(lat)]=np.rint((atlas[y0,x0,:3]*(1-fx)+atlas[y0,x1,:3]*fx)*(1-fy)+
                  (atlas[y1,x0,:3]*(1-fx)+atlas[y1,x1,:3]*fx)*fy).astype(np.uint8)
    p=TEXTURES/'saturn_body_reference.png';Image.fromarray(result).save(p)
    artifact(p,['saturn_glb'],observationDate='NASA VTAD model released 2019; texture observation dates not supplied',
             units='display RGB',datum='Body-fixed equator/north aligned; prime-meridian registration not supplied by NASA asset',
             colorSpace='sRGB base color; UE sRGB=true',dimensions=[4096,2048],
             process='Reproject NASA cube atlas to equirectangular using actual model vertices and UV coordinates; bilinear sample.',
             scientificLimit='NASA visualization asset; not a radiometrically calibrated Cassini albedo product.',credit='NASA Visualization Technology Applications and Development (VTAD)')
    rings=np.loadtxt(RAW/'UVIS_HSP_2005_139_126TAU_E_TAU01KM.TAB',delimiter=',')
    table=np.column_stack([rings[:,0]*1000,rings[:,4],rings[:,5],rings[:,11]])
    p=DATA/'saturn_ring_profile.csv'
    with p.open('w',encoding='utf-8',newline='\n') as f:
        np.savetxt(f,table,delimiter=',',header='radiusMeters,normalOpticalDepth,maxDetectableOpticalDepth,noteFlag',comments='',fmt=['%.0f','%.4f','%.4f','%.0f'])
    artifact(p,['rings_profile'],observationDate='2005-05-19T05:33:44.995..14:25:12.667',units='meters, dimensionless optical depth, bit flags',
             datum='Saturn equatorial plane, Cassini UVIS 126 Tau egress; PDS CO-SR-UVIS-HSP-2/4-OCC-V3.0',
             colorSpace='not applicable',process='Retain radius, measured tau, detection ceiling and flags; km converted to meters without resampling.',
             scientificLimit='Single stellar occultation; negative optical depth can be noise; opaque regions are measurement-limited.',
             credit='NASA PDS Ring-Moon Systems Node; Cassini UVIS team; Joshua E. Colwell')
    ringpos=accessor(g,b,4);radii=np.linalg.norm(ringpos[:,[0,2]],axis=1)/500*60268000
    inner,outer=float(radii.min()),float(radii.max())
    tex=np.asarray(images[1]);radius=np.linspace(inner,outer,tex.shape[1])
    tau=np.interp(radius,table[:,0],table[:,1]);ceiling=np.interp(radius,table[:,0],table[:,2])
    clamped=np.clip(tau,0,np.maximum(ceiling,0))
    alpha=np.rint((1-np.exp(-clamped))*255).astype(np.uint8)
    # NASA texture defines observed ring color, UVIS controls normal-incidence attenuation.
    tex=tex.copy();tex[:,:,3]=alpha[None,:]
    p=TEXTURES/'saturn_rings_rgba.png';Image.fromarray(tex).save(p)
    artifact(p,['saturn_glb','rings_profile'],observationDate='color: unspecified NASA VTAD model; optical depth: 2005-05-19',
             units='RGB display color; alpha dimensionless',datum='Saturn equatorial plane; u=(r-innerRadius)/(outerRadius-innerRadius)',
             colorSpace='RGB sRGB; alpha linear; UE sRGB=true',dimensions=[tex.shape[1],tex.shape[0]],
             process='Preserve NASA radial strip RGB; interpolate UVIS optical depth to strip radii; alpha=1-exp(-clamp(tau,0,maxDetectableTau)).',
             scientificLimit='Visualization combines sources. Alpha is a normal-incidence approximation, not view-dependent scattering. No measured particle coordinates.',
             credit='NASA VTAD; NASA PDS Ring-Moon Systems Node; Cassini UVIS team')
    save_json(DATA/'saturn_rings.json',dict(schemaVersion=1,innerRadiusMeters=inner,outerRadiusMeters=outer,
              texturePath=p.relative_to(ROOT).as_posix(),profilePath='Content/Star/Data/saturn_ring_profile.csv',
              uv='u=(radiusMeters-innerRadiusMeters)/(outerRadiusMeters-innerRadiusMeters); v=0.5',
              plane='IAU_SATURN equator; local normal +Z; transform using Saturn bodyFixedToEclipticJ2000',
              profileRadialSamplingMeters=1000,alpha='1-exp(-tauNormal); use tau/abs(dot(view,normal)) for line-of-sight transmission',
              note='Visible main rings only. D, F and E ring geometry is not represented by this strip.'))
    p=TEXTURES/'saturn_cassini_PIA05389.jpg';shutil.copyfile(RAW/'PIA05389.jpg',p)
    artifact(p,['saturn_reference'],observationDate='2004-03-27',units='display RGB',datum='Cassini camera view; not an albedo map',
             colorSpace='display RGB',process='Byte-preserved official natural-color reference.',credit='NASA/JPL/Space Science Institute')

def apollo():
    path=RAW/'NAC_DTM_APOLLO17.TIF'
    with tifffile.TiffFile(path) as t: geo=t.geotiff_metadata; source=t.asarray()
    spacing=geo['ModelPixelScale'][0];x_origin,y_origin=geo['ModelTiepoint'][3:5]
    lat,lon=20.1908,30.7717;radius=1737400.0;standard=math.radians(20)
    x=radius*math.cos(standard)*math.radians(lon-180);y=radius*math.radians(lat)
    center_col=(x-x_origin)/spacing;center_row=(y_origin-y)/spacing
    width,height=2800,2400;col=int(round(center_col-width/2));row=int(round(center_row-height/2))
    h=source[row:row+height,col:col+width].copy();mask=np.isfinite(h)&(h>-1e30)
    assert h.shape==(height,width) and mask.mean()>0.8
    # Invalid cells remain explicitly invalid; runtime must use mask and fallback to LOLA there.
    h[~mask]=0
    p=DATA/'apollo17_height_f32.bin';h.astype('<f4').tofile(p)
    maskpath=DATA/'apollo17_valid_u8.bin';mask.astype(np.uint8).tofile(maskpath)
    ortho=tifffile.imread(RAW/'NAC_DTM_APOLLO17_MOSAIC_5M.TIF')[row:row+height,col:col+width]
    rgba=np.stack([ortho,ortho,ortho,mask.astype(np.uint8)*255],axis=-1)
    tex=TEXTURES/'apollo17_ortho_5m.png';Image.fromarray(rgba).save(tex)
    left=x_origin+col*spacing;top=y_origin-row*spacing
    meta=dict(schemaVersion=1,binaryPath=p.relative_to(ROOT).as_posix(),validityPath=maskpath.relative_to(ROOT).as_posix(),
              texturePath=tex.relative_to(ROOT).as_posix(),width=width,height=height,format='float32',endianness='little',
              layout='row-major',headerBytes=0,units='meters above 1737400m sphere',referenceRadiusMeters=radius,
              validMask='uint8 row-major, 1=measured DTM, 0=no source data; heights at invalid cells are zero and MUST NOT be sampled',
              projection='equirectangular',centralMeridianDegrees=180,standardParallelDegrees=20,latitudeOfOriginDegrees=0,
              topLeftPixelCornerProjectedMeters=[left,top],pixelSpacingMeters=spacing,
              westLongitudeDegrees=180+math.degrees(left/(radius*math.cos(standard))),
              eastLongitudeDegrees=180+math.degrees((left+width*spacing)/(radius*math.cos(standard))),
              northLatitudeDegrees=math.degrees(top/radius),southLatitudeDegrees=math.degrees((top-height*spacing)/radius),
              geographicSampleCoordinates='x=(lon-west)/(east-west)*width-0.5; y=(north-lat)/(north-south)*height-0.5',
              rowOrder='north-to-south',columnOrder='west-to-east',datumFrame='MOON_ME',
              longitudeConvention='planetocentric east-positive',
              longitudeAtPixelCenter='180 + degrees((left+(column+0.5)*spacing)/(radius*cos(20deg)))',
              latitudeAtPixelCenter='degrees((top-(row+0.5)*spacing)/radius)',
              sampleCoordinates='x=(radius*cos(20deg)*radians(lon-180)-left)/spacing-0.5; y=(top-radius*radians(lat))/spacing-0.5',
              sourceWindow=[col,row,width,height],intendedCenterDegrees=[lat,lon],coverageMeters=[width*spacing,height*spacing],
              validFraction=float(mask.mean()),minHeightMeters=float(h[mask].min()),maxHeightMeters=float(h[mask].max()),
              sourcePostSpacingMeters=5,reportedLolaTieRmsMeters=1.77,reportedPrecisionMeters=2.55,
              scientificLimit='Official LROC 5m DTM/5m browse orthophoto, not the unavailable USGS0.5m orthomosaic. Orthophoto includes baked solar shading.')
    save_json(DATA/'apollo17.json',meta)
    for output in [p,maskpath,DATA/'apollo17.json']:
        artifact(output,['apollo17_dem'],observationDate='2010-07-28T19:35:36..2016-03-17T19:55:14; product2020-10-13',
                 units='meters/validity/georeferencing',datum='Moon ME, 1737400m sphere, planetocentric east-longitude',colorSpace='not applicable',
                 process=f'Native 5m crop window {col},{row},{width},{height}; exact samples; explicit invalid mask, no invented terrain.',
                 credit='NASA/GSFC/Arizona State University; LRO LROC team; Mark Robinson')
    artifact(tex,['apollo17_ortho','apollo17_dem'],observationDate='2010-07-28..2016-03-17',units='display grayscale',
             datum='Same georeferenced source pixel window as apollo17_height_f32.bin',colorSpace='display grayscale; UE sRGB=true',
             process='Exact 5m crop; copy grayscale into RGB; alpha from valid DTM mask.',
             scientificLimit='Brightness-equalized browse orthomosaic with baked lighting; not raw albedo.',
             credit='NASA/GSFC/Arizona State University; LRO LROC')

def main():
    TEXTURES.mkdir(parents=True,exist_ok=True);DATA.mkdir(parents=True,exist_ok=True)
    moon();earth_stars();saturn();apollo()
    provenance=ROOT/'Data/provenance';provenance.mkdir(parents=True,exist_ok=True)
    for name in ['NAC_DTM_APOLLO17.LBL','NAC_DTM_APOLLO17_README.TXT','UVIS_HSP_2005_139_126TAU_E_TAU01KM.LBL']:
        shutil.copyfile(RAW/('upstream_'+name),provenance/name)
    save_json(ROOT/'Data/manifest.json',dict(schemaVersion=1,assets=MANIFEST,
        gaps=['USGS Apollo17 0.5m orthomosaic/DEM bytes not retrieved; official alternative LROC5m crop delivered.',
              'NASA VTAD Saturn color source observation dates and prime-meridian registration not specified.'],
        acquisitionCatalog='Tools/Data/sources.json',offlineRuntime=True,personalApiKeysUsed=False))

if __name__=='__main__':main()
