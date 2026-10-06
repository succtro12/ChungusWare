"""Reproducible cosmetic assets. No sampled audio or copied glyph bitmap.
The optional existing mascot is NOT embedded by this script.
Usage: python build-portable-assets.py /path/to/W95F.otf
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops
import sys, math, wave, random, struct, hashlib, json

root=Path(__file__).resolve().parent
font_path=Path(sys.argv[1]);font=ImageFont.truetype(str(font_path),18)
spacing=ImageFont.truetype(str(font_path),13)
atlas=Image.new('RGBA',(1024,1024),(0,0,0,0));draw=ImageDraw.Draw(atlas)
advances=[float(spacing.getlength(chr(i+32))) for i in range(95)]
# Dedicated regular-weight rasters for every existing text-size role.
for role,px in enumerate((18,21,14,15,19)):
    raster=ImageFont.truetype(str(font_path),px)
    for i in range(95):
        c=chr(i+32);cell=Image.new('RGBA',(24,28));d=ImageDraw.Draw(cell)
        d.text((0,4),c,font=raster,fill=(255,255,255,255),stroke_width=0)
        cell.putalpha(cell.getchannel('A').point(lambda a: 255 if a >= 128 else 0))
        atlas.paste(cell,(i%42*24,role*84+i//42*28))
# Original neutral placeholder: simple containment crate. The real optional PNG
# replaces ONLY this reserved 300x332 area at y=448, never the glyphs or white texel.
draw.rectangle((52,558,248,711),fill=(115,105,87,255),outline=(65,49,34,255),width=5)
draw.rectangle((62,568,238,701),fill=(172,160,131,255))
for x in range(74,235,28):draw.rectangle((x,570,x+5,699),fill=(65,49,34,255))
draw.rectangle((80,610,220,648),fill=(220,208,175,255),outline=(65,49,34,255),width=3)
label='no specimen';w=font.getlength(label);draw.text((150-w/2,618),label,font=font,fill=(65,49,34,255))
draw.rectangle((1020,1020,1023,1023),fill=(255,255,255,255))
atlas.save(root/'ui-atlas.png');(root/'ui-atlas.rgba').write_bytes(atlas.tobytes())
header='#pragma once\nnamespace chungus_ui { inline constexpr float advances[95]={'
header+=','.join(f'{x:.6f}f' for x in advances)+'};inline constexpr int logoWidth=300,logoHeight=332; }\n'
(root.parent/'src/ui_font_metrics.h').write_text(header,encoding='utf-8')

# Original Caca impact: analytic falling sine, detuned harmonic, seeded noise
# burst and soft saturation. No input recording, sample or meme audio.
rate=44100;duration=.72;rng=random.Random(200742);phase=0.;samples=[]
for i in range(round(rate*duration)):
    t=i/rate;frequency=42+110*math.exp(-t*28);phase+=2*math.pi*frequency/rate
    attack=min(1.,t/.003);tail=math.exp(-t*7.5)
    body=.72*math.sin(phase)+.16*math.sin(phase*1.49+.2)
    transient=.14*rng.uniform(-1,1)*math.exp(-t*65)
    signal=math.tanh((body*tail+transient)*1.3)*attack*.48
    end=min(1.,(duration-t)/.04);samples.append(round(signal*end*32767))
with wave.open(str(root/'original-impact-alternative.wav'),'wb') as out:
    out.setnchannels(1);out.setsampwidth(2);out.setframerate(rate)
    out.writeframes(struct.pack('<'+'h'*len(samples),*samples))

# Read font version from the TTF name table without another build dependency.
data=font_path.read_bytes();n=struct.unpack_from('>H',data,4)[0];version='unknown'
for i in range(n):
    tag,_,offset,length=struct.unpack_from('>4sIII',data,12+i*16)
    if tag!=b'name':continue
    _,count,string_offset=struct.unpack_from('>HHH',data,offset)
    for j in range(count):
        platform,encoding,language,name,size,pos=struct.unpack_from('>HHHHHH',data,offset+6+j*12)
        if name==5:
            raw=data[offset+string_offset+pos:offset+string_offset+pos+size]
            version=raw.decode('utf-16-be' if platform in (0,3) else 'latin1');break
manifest={'font':'W95FA Regular','font_version':version,'font_size':18,'font_weight':'regular; native-pixel 18/21/14/15/19px text roles; 13px logical spacing unchanged',
          'font_source':'https://www.dafont.com/w95fa.font',
          'font_license':'SIL OFL 1.1','font_sha256':hashlib.sha256(data).hexdigest(),
          'embedded':['licensed glyph rasterization','original containment-crate placeholder','white utility texel'],
          'audio_alternative':'original analytic synthesis; NOT selected for shipping; approved embedded boom retained',
          'files':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ('ui-atlas.rgba','ui-atlas.png','original-impact-alternative.wav')}}
(root/'portable-asset-provenance.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps(manifest,indent=2))
