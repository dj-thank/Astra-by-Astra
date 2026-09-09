"""Complete portable manifest entries after data conversion and SPICE orientation."""
import json
import math
import shutil
from pathlib import Path
from acquire import ROOT, RAW, save_json, digest

def main():
    data=ROOT/'Content/Star/Data';manifest=json.loads((ROOT/'Data/manifest.json').read_text(encoding='utf-8'))
    catalog=json.loads((ROOT/'Tools/Data/sources.json').read_text(encoding='utf-8'))
    receipts={x['id']:json.loads((RAW/(x['filename']+'.receipt.json')).read_text(encoding='utf-8')) for x in catalog}
    for name in ['NAC_DTM_APOLLO17.LBL','NAC_DTM_APOLLO17_README.TXT','UVIS_HSP_2005_139_126TAU_E_TAU01KM.LBL']:
        shutil.copyfile(RAW/('upstream_'+name),ROOT/'Data/provenance'/name)
    # Native lunar equirectangular raster is also a regular geographic lat/lon grid.
    path=data/'apollo17.json';m=json.loads(path.read_text(encoding='utf-8'))
    left,top=m['topLeftPixelCornerProjectedMeters'];r=m['referenceRadiusMeters'];s=m['pixelSpacingMeters'];cos=math.cos(math.radians(20))
    m.update(westLongitudeDegrees=180+math.degrees(left/(r*cos)),
             eastLongitudeDegrees=180+math.degrees((left+m['width']*s)/(r*cos)),
             northLatitudeDegrees=math.degrees(top/r),southLatitudeDegrees=math.degrees((top-m['height']*s)/r),
             geographicSampleCoordinates='x=(lon-west)/(east-west)*width-0.5; y=(north-lat)/(north-south)*height-0.5')
    save_json(path,m)
    credits=[{'id':'moon','nameJa':'月の色・全球地形','credit':'NASA Scientific Visualization Studio / NASA GSFC / MIT / Arizona State University; LRO LROC・LOLA'},
             {'id':'apollo17','nameJa':'Apollo 17地形・画像','credit':'NASA / GSFC / Arizona State University; LRO LROC team'},
             {'id':'earth','nameJa':'地球昼夜画像','credit':'NASA Earth Observatory / NOAA NGDC / MODIS・Suomi NPP VIIRS teams'},
             {'id':'stars','nameJa':'星空','credit':'NASA Scientific Visualization Studio / Ernie Wright; Hipparcos・Tycho・Gaia DR2'},
             {'id':'saturn','nameJa':'土星・環','credit':'NASA VTAD / NASA JPL / Space Science Institute / PDS Ring-Moon Systems Node / Cassini UVIS team'},
             {'id':'ephemeris','nameJa':'天体位置・姿勢','credit':'NASA JPL Horizons / NAIF SPICE'}]
    save_json(data/'credits_ja.json',{'schemaVersion':1,'credits':credits})
    present={a['path']:a for a in manifest['assets']}
    additional={
        'bodies.json':(['pck00011.tpc','naif0012.tls','moon_pa_de421_1900-2050.bpc','moon_080317.tf'],
                       json.loads((data/'bodies.json').read_text(encoding='utf-8'))['epoch'],'meters, meters/second, dimensionless rotation matrix',
                       'SSB origin; ecliptic J2000; MOON_ME and IAU body-fixed frames',
                       'Horizons geometric KM-S vectors converted to SI; SPICE pxform gives body-to-ecliptic orientation.'),
        'sky_mapping.json':(['stars'],'catalog composite 2020','radians/degrees, unit vectors','ecliptic J2000 to equatorial ICRF/J2000',
                            'IAU J2000 mean obliquity rotation plus inward celestial equirectangular UV convention.'),
        'saturn_rings.json':(['saturn_glb','rings_profile'],'2005-05-19 optical-depth observation; NASA VTAD model2019','meters, dimensionless tau',
                            'Saturn equatorial plane','Extract source mesh radial bounds using60268000m equatorial reference radius; publish radial UV mapping.'),
        'credits_ja.json':(['moon_albedo','moon_dem','earth_day','earth_night','stars','saturn_glb','rings_profile'],
                          'various source epochs retained per asset','text','not applicable','Aggregate source credits for offline Japanese UI.')}
    for name,(ids,date,units,datum,process) in additional.items():
        path=data/name;rel=path.relative_to(ROOT).as_posix()
        present[rel]=dict(path=rel,sha256=digest(path),bytes=path.stat().st_size,sources=[receipts[x] for x in ids],
                          observationDate=date,units=units,datum=datum,colorSpace='not applicable',process=process,credit='NASA/JPL/NAIF and original data teams; see credits_ja.json')
        if name=='bodies.json':
            bodydata=json.loads(path.read_text(encoding='utf-8'))
            present[rel]['horizonsSources']=bodydata['provenance']
    for a in present.values():
        path=ROOT/a['path'];a['sha256']=digest(path);a['bytes']=path.stat().st_size
        support=['apollo17_label','apollo17_readme'] if 'apollo17' in path.name else (['rings_label'] if 'ring' in path.name else [])
        existing={x['id'] for x in a['sources']}
        a['sources'].extend(receipts[x] for x in support if x not in existing)
    manifest['assets']=list(present.values())
    manifest['sourceAcquisitionReceipts']=list(receipts.values())
    manifest['localMcp']={'package':'@programcomputer/nasa-mcp-server','version':'1.0.14','scope':'local stdio, authoring only',
                          'receipt':'Data/nasa_mcp_receipt.json','includedInRuntime':False}
    save_json(ROOT/'Data/manifest.json',manifest)
    mcp=json.loads((ROOT/'work/mcp/receipt.json').read_text(encoding='utf-8'))
    mcp['packageLockSha256']=digest(ROOT/'Tools/Data/package-lock.json')
    mcp['horizonsResultContainsActualEphemeris']='$$SOE' in (ROOT/'work/mcp/jpl_horizons.json').read_text(encoding='utf-8')
    mcp['imageResponseContainsActualImage']='"type": "image"' in (ROOT/'work/mcp/nasa_images.json').read_text(encoding='utf-8')
    save_json(ROOT/'Data/nasa_mcp_receipt.json',mcp)
    print('Final manifest',len(manifest['assets']),'artifacts',sum(a['bytes'] for a in manifest['assets']),'bytes')

if __name__=='__main__':main()
