"""Separate decorative diffuse photography from resolved NASA catalogue stars."""
from pathlib import Path
import hashlib
import json
import shutil
import sys
from PIL import Image, ImageFilter

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/"work/v03/pythonlibs"))
import OpenEXR

source=ROOT/"Content/Star/Art/PhotoSky/stars_photo_8k.png"
stars=ROOT/"work/v03/sky/hiptyc_2020_16k.exr"
expected="bd12d36e3e32ba75e61bd501aca25675f70250dce50fcfb880f22b9b6229725c"
if hashlib.sha256(stars.read_bytes()).hexdigest()!=expected:raise ValueError("NASA source hash mismatch")
exr=OpenEXR.InputFile(str(stars));window=exr.header()["dataWindow"]
if (window.max.x-window.min.x+1,window.max.y-window.min.y+1)!=(16384,8192):raise ValueError("Expected original 16K source")
out=ROOT/"Content/Star/Art/PhotoSkyV3";out.mkdir(parents=True,exist_ok=True)
photo=Image.open(source).convert("RGB")
# Suppress compact photograph stars before adding the sharper catalogue layer.
# Broad diffuse light remains cosmetic, with the original approximate registration.
diffuse=photo.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(1.0))
diffuse.save(out/"eso_diffuse_8k.png")
shutil.copyfile(stars,out/stars.name)
receipt={"status":"SOURCE_PROCESSED_NOT_GPU_VERIFIED","source_url":"https://svs.gsfc.nasa.gov/vis/a000000/a004800/a004851/hiptyc_2020_16k.exr",
    "source_page":"https://svs.gsfc.nasa.gov/4851/","credits":"NASA/Goddard Scientific Visualization Studio/Ernie Wright; ESO/S. Brunier CC BY 4.0",
    "catalogue":"Hipparcos-2 and Tycho-2 bright-star map, 2020 product; catalogue visualization, not a photograph",
    "frame":"ICRF/J2000, RA increases left","color_space":"EXR linear HALF RGB; diffuse PNG sRGB",
    "stars_size":[16384,8192],"stars_sha256":expected,
    "diffuse_source_sha256":hashlib.sha256(source.read_bytes()).hexdigest(),
    "diffuse_sha256":hashlib.sha256((out/"eso_diffuse_8k.png").read_bytes()).hexdigest(),
    "diffuse_processing":"9px morphological opening then 1px Gaussian; estimated separation, not an observed starless sky",
    "limitations":["ESO diffuse registration remains approximate at degree scale","display intensity is artistic, not radiometric","broad unresolved light remains in the diffuse layer"]}
(ROOT/"Data/photo_sky_v03.json").write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(receipt))
