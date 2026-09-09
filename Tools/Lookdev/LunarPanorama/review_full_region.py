from pathlib import Path
import json,numpy as np
from PIL import Image,ImageDraw,ImageFont
root=Path('.');reg=json.loads(Path('Data/lunar_panorama_full_region_registration.json').read_text());out=Path('Content/Star/Art/LunarPanorama/FullRegion/Review');font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',20)
for c in reg['cameras']:
 sheet=Image.new('RGB',(1440,1530),(20,23,28));draw=ImageDraw.Draw(sheet);h=c['independentPhotoHoldout'];panels=[(Path(f"Content/Star/Art/LunarPanorama/Source/AS17-147-{c['frame']}HR.jpg"),f"NASA AS17-147-{c['frame']} / fit source"),(out/(c['direction']+'_reference.png'),'Measured terrain + photo candidate'),(Path(f"Content/Star/Art/LunarPanorama/Source/AS17-147-{h['frame']}HR.jpg"),f"NASA AS17-147-{h['frame']} / independent photo"),(out/(c['direction']+'_independent.png'),'Transferred camera / not re-fitted')]
 for i,(path,label) in enumerate(panels):
  im=Image.open(path).convert('RGB');im.thumbnail((716,716));x=(i%2)*720;y=45+(i//2)*745;sheet.paste(im,(x,y));draw.text((x+8,y-28),label,font=font,fill='white')
 draw.text((8,1500),f"{c['direction']} | point holdout {c['holdoutRmsDegrees']:.3f} deg | independent photo {h['rmsDegrees']:.3f} deg | gray = untextured / near excluded",font=font,fill='white');sheet.save(out/(c['direction']+'_comparison.jpg'),quality=92)
