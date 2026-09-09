"""Deterministic observed-photo assets; no invented texture or terrain.

python Tools/Lookdev/PhotoMoon/build.py [--download]
Raw data stays in work/raw; derived deliverables carry SHA256 provenance.
Requires numpy, scipy and Pillow. Never starts UE or changes terrain.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import urllib.request
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter

ROOT = Path(__file__).resolve().parents[3]
RAW = ROOT / 'work/raw'
OUT = ROOT / 'Content/Star/Art/PhotoMoon'
URL = 'https://data.lroc.im-ldi.com/data/35mm/AS11/raw/AS11-45-6705A.tif'
RAW_SHA = '97448acfae5df94b20514075a7f796695e9b1587dc49086deafeaa947799b015'
PAGE = 'https://data.lroc.im-ldi.com/apollo/view?camera=A&image_name=AS11-45-6705'
SCALE_PAGE = 'https://www.nasa.gov/wp-content/uploads/static/history/alsj/a11/images11.html'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def smoothstep(a, b, x):
    t = np.clip((x-a)/(b-a), 0, 1)
    return t*t*(3-2*t)


def save(path, a):
    Image.fromarray(np.round(np.clip(a, 0, 1)*255).astype(np.uint8)).save(path)


def resize_float(a, size, method=Image.Resampling.BILINEAR):
    return np.asarray(Image.fromarray(np.asarray(a,dtype=np.float32)).resize(size,method),dtype=np.float32)


def runtime_products(regional, detail, normal):
    # The original photo window remains unchanged. This interpolates to POT for
    # UE mip/streaming compatibility, not to increase observational resolution.
    size=(4096,4096)
    confidence=regional[:,:,1]*regional[:,:,3]
    weight=resize_float(confidence,size)
    numerator=resize_float((regional[:,:,0]*2-1)*confidence,size)
    # Nearest support is a hard rejection: filtered valid neighbours cannot
    # reintroduce a source shadow/clipped/invalid pixel into the texture.
    support=resize_float((confidence>0).astype(np.float32),size,Image.Resampling.NEAREST)>0
    alpha=resize_float(regional[:,:,3],size,Image.Resampling.NEAREST)
    support &= alpha>0
    residual=np.divide(numerator,weight,out=np.zeros_like(numerator),where=(weight>1e-8)&support)
    weight=np.where(support,weight,0)
    pot=np.stack([.5+.5*np.clip(residual,-.32,.32),weight,np.full_like(weight,.5),alpha],-1)
    save(OUT/'apollo17_photo_regional_4096_linear.png',pot)
    small=np.stack([resize_float(detail[:,:,i],(1024,1024)) for i in range(3)],-1)
    save(OUT/'apollo_soil_detail_1024_linear.png',small)
    # Filter signed vectors, then normalize, rather than treating encoded RGB
    # as scalar colour. The physical square tile remains 38.180880mm.
    filtered=np.stack([resize_float(normal[:,:,i],(1024,1024)) for i in range(3)],-1)
    filtered/=np.maximum(np.linalg.norm(filtered,axis=-1,keepdims=True),1e-8)
    save(OUT/'apollo_soil_normal_1024_dx.png',.5+.5*filtered)


def build(download=False):
    RAW.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    raw = RAW / 'AS11-45-6705A.tif'
    if not raw.exists() and download:
        urllib.request.urlretrieve(URL, raw)
    if not raw.exists():
        raise FileNotFoundError('Run with --download to obtain NASA/JSC/ASU raw scan')
    if sha(raw) != RAW_SHA:
        raise ValueError('Raw scan SHA256 mismatch: inspect source before processing')
    source = ROOT/'Content/Star/Textures/apollo17_ortho_5m.png'
    shutil.copyfile(source, RAW/source.name)
    meta = json.loads((ROOT/'Content/Star/Data/apollo17.json').read_text(encoding='utf-8'))
    ortho = np.asarray(Image.open(source), dtype=np.float32)/255
    gray = ortho[:,:,0]
    # Treat browse grayscale as display-coded. This is estimated illumination
    # suppression, NOT recovery of calibrated radiance/albedo or missing shadows.
    linear = np.where(gray <= .04045, gray/12.92, ((gray+.055)/1.055)**2.4)
    log = np.log(np.maximum(linear, .003))
    low = gaussian_filter(log, 24)  # 120m sigma; retain local source features
    ratio = np.clip((log-low)*.28, -.32, .32)
    confidence = smoothstep(.10,.26,gray)*(1-smoothstep(.90,.99,gray))*ortho[:,:,3]
    # Steep radiometric steps (mosaic strip joins) are not geological boundaries.
    gy, gx = np.gradient(gaussian_filter(gray,2))
    confidence *= 1-smoothstep(.02,.07,np.hypot(gx,gy))
    confidence = gaussian_filter(confidence,1)*(gray>.10)*(gray<.99)
    regional = np.stack([.5+.5*ratio,confidence,np.full_like(gray,.5),ortho[:,:,3]],-1)
    save(OUT/'apollo17_photo_regional_linear.png',regional)

    full = Image.open(raw).convert('RGB')
    # Inspected raw frame: black film margins x<316 and x>=2738.
    # Interior square avoids prominent large shiny clast in lower-right frame.
    crop_box = [650,120,1750,1220]
    crop = full.crop(crop_box)
    crop.save(OUT/'as11_6705a_observed_crop.png')
    p = np.asarray(crop,dtype=np.float32)/255
    # Neutral monochrome avoids claiming film's cyan cast as lunar true color.
    lum = p@np.array([.2126,.7152,.0722],dtype=np.float32)
    l = np.where(lum<=.04045,lum/12.92,((lum+.055)/1.055)**2.4)
    local = gaussian_filter(l,28)
    residual = np.clip(np.log(np.maximum(l,.003)/np.maximum(local,.003))*.20,-.30,.30)
    conf = smoothstep(.07,.19,lum)*(1-smoothstep(.62,.83,lum))
    # Fade observed crop boundaries to neutral, retaining the interior pixels'
    # positions. No mirroring, resynthesis, random tiles or fictitious rocks.
    yy,xx=np.mgrid[:1100,:1100]
    edge=smoothstep(0,75,np.minimum.reduce([xx,yy,1099-xx,1099-yy]))
    residual *= conf*edge
    detail=np.stack([.5+.5*residual,np.full_like(l,.5),conf*edge],-1)
    save(OUT/'apollo_soil_detail_linear.png',detail)
    # Conservative image-gradient relief estimate, not stereo/DEM measurement.
    # Suppress sharp source shadows before deriving normals. z remains positive.
    h=gaussian_filter(residual,2)
    dy,dx=np.gradient(h)
    n=np.stack([-dx*12,-dy*12,np.ones_like(h)],-1)
    # Bound the estimated slope; film dust/highlights cannot create steep relief.
    slope=np.linalg.norm(n[:,:,:2],axis=-1)
    n[:,:,:2]*=np.minimum(1,.25/np.maximum(slope,1e-8))[:,:,None]
    n/=np.linalg.norm(n,axis=-1,keepdims=True)
    save(OUT/'apollo_soil_normal_dx.png',.5+.5*n)
    runtime_products(regional,detail,n)

    # Fixed source/derived comparison, not a renderer or alternate-light proof.
    board=Image.new('RGB',(1600,900),(24,24,24));d=ImageDraw.Draw(board)
    panels=[(Image.open(source),'Observed LROC Apollo17 / 5m'),
            (Image.fromarray(np.uint8(np.clip(regional[:,:,0]*255,0,255))),'Estimated de-lit photo modulation'),
            (crop,'Observed Apollo11 ALSCC crop / ~38mm'),
            (Image.open(OUT/'apollo_soil_detail_linear.png').getchannel('R'),'Linear soil modulation (R channel)')]
    for i,(im,title) in enumerate(panels):
        x=(i%2)*800;y=(i//2)*450
        im=im.convert('RGB');im.thumbnail((780,415));board.paste(im,(x+(800-im.width)//2,y+30))
        d.text((x+12,y+9),title,fill='white')
    board.save(OUT/'source_comparison.png')
    crop_m=[.083*(1100/(2738-316)),.072*(1100/2048)]
    # A scalar remains compatible with current graph; <3% aspect approximation
    # explicitly reported instead of pretending the film scan is metric truth.
    tile_m=float(np.sqrt(crop_m[0]*crop_m[1]))
    provenance={
      'schemaVersion':1,'status':'LOCAL_ASSET_CANDIDATE_NOT_RUNTIME_VALIDATED',
      'sources':[
        {'id':'apollo17_lroc','path':source.relative_to(ROOT).as_posix(),'sha256':sha(source),
         'rawDistribution':'https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/LROLRC_2001/EXTRAS/BROWSE/NAC_DTM/APOLLO17/NAC_DTM_APOLLO17_MOSAIC_5M.TIF',
         'rawDistributionSha256':'8e3a2dff013059278a58daed87570cb963e37de46ef0639c95afd3a434fc92d7',
         'observationDate':'2010-07-28..2016-03-17','credit':'NASA/GSFC/Arizona State University; LRO LROC team',
         'pixelSpacingMeters':5,'sourceWindow':meta['sourceWindow'],'datum':'MOON_ME; east longitude, planetocentric latitude; radius 1737400m'},
        {'id':'as11_45_6705a','url':URL,'sourcePage':PAGE,'rawPath':raw.relative_to(ROOT).as_posix(),'sha256':sha(raw),
         'observationDate':'1969-07-20','credit':'NASA/JSC/Arizona State University','rawImagePixels':list(full.size),
         'rights':'Unprocessed raw scan public domain; https://apollo.im-ldi.com/ABOUT_SCANS/index.html (Acceptable Use)',
         'observedObject':'Apollo11 loose aggregate lunar soil; used as LUNAR analog, NOT Apollo17 site observation',
         'fieldOfViewMeters':[.083,.072],'scaleSource':SCALE_PAGE,'fieldBoundsPx':[316,0,2738,2048],
         'cropBoxPx':crop_box,'cropPhysicalSizeMeters':crop_m,'runtimeSquareTileMeters':tile_m,
         'scaleUncertainty':'Approximate scan-margin identification and square sampling (~3% aspect); terrain not flat; no photogrammetric stereo calibration claimed'}],
      'regionalPhotometry':{'method':'log high-pass sigma=24px (120m); residual gain .28 clamped +/- .32; shadow/saturation/strip-edge confidence',
         'channels':{'R':'0.5+0.5*estimated relative reflectance residual','G':'confidence, shadow/saturation suppressed','B':'0.5 unused','A':'DTM validity'},
         'limit':'Single-image de-lighting is non-unique; residual baked shadow remains possible. Global measured lunar albedo remains base. No ortho-derived normals.'},
      'nearPhotometry':{'method':'Linear grayscale; 28px low-frequency normalization, bounded residual; masked shadow/specular; 75px neutral border',
         'normal':'Estimated image-gradient tangent relief; not measured stereo heights; no displacement',
         'roughness':'Constant artistic dusty regolith .94; NOT measured from photograph',
         'registration':'UV1 tangent chart in meters; lunar-analog tile, not geographic placement of Apollo11 clasts at Apollo17'},
      'runtimeResampling':{'regional':'2800x2400 -> 4096x4096 normalized weighted bilinear residual/confidence; nearest zero-confidence support rejects invalid source pixels; bounds unchanged. Upsampling adds NO new observed detail; native source remains 5m.',
         'soil':'1100x1100 -> 1024x1024 bilinear, same 38.180880mm tile; original observed crop preserved',
         'normal':'Signed XYZ bilinear downsample then unit renormalization then UNORM encoding; no sRGB filtering'},
      'geographicBounds':{k:meta[k] for k in ['westLongitudeDegrees','eastLongitudeDegrees','northLatitudeDegrees','southLatitudeDegrees']},
      'artifacts':[]}
    for path in sorted(OUT.glob('*.png')):
        provenance['artifacts'].append({'path':path.relative_to(ROOT).as_posix(),'sha256':sha(path),'pixels':list(Image.open(path).size)})
    write_json(ROOT/'Data/photo_moon_provenance.json',provenance)
    recipe={'material':'M_Surface','status':'ROOT_UE_WIRING_REQUIRED',
      'scalars':{'AlbedoScale':1.0,'RegionalStrength':.65,'DetailScale':1/tile_m,'DetailStrength':.30,'SurfaceRoughness':.94,'NormalStrength':.20},
      'textures':{
       'RegionalTex':{'key':'apollo17_photo_regional','file':'Content/Star/Art/PhotoMoon/apollo17_photo_regional_4096_linear.png','srgb':False,'compression':'TC_MASKS','sampler_type':'SAMPLERTYPE_MASKS','address_x':'TA_CLAMP','address_y':'TA_CLAMP','mips':True},
       'DetailTex':{'key':'apollo_soil_detail','file':'Content/Star/Art/PhotoMoon/apollo_soil_detail_1024_linear.png','srgb':False,'compression':'TC_MASKS','sampler_type':'SAMPLERTYPE_MASKS','address_x':'TA_WRAP','address_y':'TA_WRAP','mips':True},
       'DetailNormalTex':{'key':'apollo_soil_normal','file':'Content/Star/Art/PhotoMoon/apollo_soil_normal_1024_dx.png','srgb':False,'compression':'TC_NORMALMAP','sampler_type':'SAMPLERTYPE_NORMAL','flip_green':False,'address_x':'TA_WRAP','address_y':'TA_WRAP','mips':True}},
      'surfaceCustom':{'source':'Shaders/Star/Surface.ush','additionalInputs':{'RegionalTex':'TextureObject','RegionalStrength':'float1'},'existingInputsUnchanged':True},
      'normalCustom':{'source':'Shaders/Star/SurfaceNormal.ush','inputs':['DetailUV','DetailScale','NormalStrength','DetailNormal'],'DetailNormal':'decoded UE NORMAL TextureSample RGB, coordinates UV1*DetailScale; no second 2x-1'},
      'meshContract':'Existing UV0=(lon+180)/360,(90-lat)/180; UV1 stable tangent meters. Keep existing tangent handedness. No geometry changes.',
      'rawPhotoImport':'Do not use as11_6705a_observed_crop.png as runtime albedo; it retains exposure/shadows for source comparison only.',
      'acceptanceViews':['Apollo17 regional overhead: identify same crater positions as source_comparison','Apollo17 normal flight view at existing moon.png camera','Apollo17 close ground view: grain physical scale, no meter-sized ALSCC clasts','Same surface alternate Sun direction: no double dark crater walls','Regional rectangle edge: smooth blend to global; out-of-range no repeating regional crop'],
      'runtimeLimits':['UV1 chart reset phase continuity not claimed','5m regional imagery cannot supply submeter geography','Apollo11 soil analog is not an Apollo17 sample','Root must import compile cook run and compare packaged captures']}
    write_json(ROOT/'Tools/Lookdev/PhotoMoon/material_recipe.json',recipe)
    print(json.dumps({'tileMeters':tile_m,'DetailScale':1/tile_m,'files':len(provenance['artifacts']),'rawSha256':sha(raw)}))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--download',action='store_true')
    build(parser.parse_args().download)
