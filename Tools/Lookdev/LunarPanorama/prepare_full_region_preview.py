from pathlib import Path
from PIL import Image
import numpy as np
p=Path('Content/Star/Art/LunarPanorama/FullRegion/apollo17_full_region_photo_rgba.png');im=Image.open(p).convert('RGBa').resize((4096,4096),Image.Resampling.LANCZOS).convert('RGBA');a=np.asarray(im).astype(np.float32)/255;c=a[:,:,:3];c=np.where(c<=.04045,c/12.92,((c+.055)/1.055)**2.4);mixed=c*a[:,:,3:]+.055*(1-a[:,:,3:]);mixed=np.where(mixed<=.0031308,mixed*12.92,1.055*mixed**(1/2.4)-.055);Image.fromarray((mixed.clip(0,1)*255).astype(np.uint8)).save('work/lunar-panorama/full-region/preview_texture.png')
