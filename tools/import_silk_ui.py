"""Compile the silk interface palette, illustration, logo and font subsets."""
from pathlib import Path
import re
import json
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from game_font import FONT_PATH, raster, FALLBACK_GLYPHS

ROOT=Path(__file__).resolve().parents[1]
# Legacy palette retained for explicit rollback. The build-owned theme JSON
# below overrides these sixteen UI slots for the authentic light theme.
#   paper surface text_ink muted_ink rule_ink cinnabar indigo jade
#   ochre focus_paper light_ink warning_ink pale_paper shadow_ink seal_ink disabled
THEME = {
    'navy': (16,31,48), 'navy_light': (27,48,67), 'navy_dark': (8,18,30),
    'navy_mid': (22,40,56), 'azure': (143,184,221),
    'gold': (213,172,89), 'gold_light': (240,213,142), 'gold_dark': (128,93,46),
    'cream': (244,229,189), 'muted': (169,158,136),
    'red': (129,31,41), 'red_light': (170,52,54),
    'jade': (84,117,100), 'disabled': (125,133,138),
}
ui_rgb=[THEME['navy'],            # paper      page / panel ground
        THEME['navy_dark'],       # surface    deeper ground
        THEME['cream'],           # text_ink   body text on navy
        THEME['muted'],           # muted_ink  secondary text
        THEME['gold_dark'],       # rule_ink   hairlines
        THEME['red_light'],       # cinnabar   selection / accent
        THEME['azure'],           # indigo     friendly side
        THEME['jade'],            # jade       positive
        THEME['gold'],            # ochre      highlight
        THEME['navy_light'],      # focus_paper raised surface / navy ramp 0
        THEME['navy_mid'],        # light_ink   navy ramp 1
        THEME['red'],             # warning_ink danger
        THEME['navy_light'],      # pale_paper  chip surface
        THEME['navy_dark'],       # shadow_ink  deep shade
        THEME['red'],             # seal_ink    seals
        THEME['disabled']]        # disabled_ink
map_rgb=[(222,211,185),(244,235,215),(160,134,81),(171,79,57),(101,135,144),
         (147,158,113),(201,188,157),(180,156,108),(111,127,91),(226,212,170),
         (153,134,102),(234,225,203),(133,157,153),(181,162,133),(201,177,124),(191,184,164)]
# Window bevel tones in the last ten slots (246-255). 246-249 stay a warm paper
# ramp for light detail panels; 250-255 are the shared ink/edge/gold accents.
tone_5bit=[(29,27,22),(28,25,20),(27,24,18),(24,21,15),(6,5,4),
           (29,26,17),(31,28,17),(16,11,6),(1,1,2),(28,26,21)]
# Build-owned theme selection: never replace the 80-entry palette with a
# 16-entry fragment. Artwork (182..229) and terrain (230..245) stay intact.
theme_name=json.loads((ROOT/'assets/ui/ui-theme.json').read_text(encoding='utf-8'))['theme']
if theme_name not in ('authentic','legacy'):
    raise ValueError('Unknown UI theme: '+theme_name)
if theme_name=='authentic':
    theme=json.loads((ROOT/'assets/ui/gba-authentic-redesign/theme-authentic.json').read_text(encoding='utf-8'))
    def color(section,key):
        value=theme[section][key].lstrip('#')
        return tuple(int(value[i:i+2],16) for i in (0,2,4))
    ui_roles=[('background','primary'),('window','fill'),('text','primary'),('text','secondary'),
        ('border','dark'),('selection','active'),('accent','indigo'),('accent','jade'),
        ('text','highlight'),('selection','background'),('background','light'),('special','warning'),
        ('background','primary'),('window','shadow'),('special','seal'),('special','disabled')]
    ui_rgb=[color(*role) for role in ui_roles]
    tone_roles=[('background','light'),('background','primary'),('background','secondary'),
        ('background','dark'),('border','dark'),('border','light'),('accent','gold'),
        ('accent','gold_dark'),('window','shadow'),('window','fill')]
    tone_5bit=[tuple(round(v*31/255) for v in color(*role)) for role in tone_roles]
# Palette index baked into silk_logo_pixels. It must stay the near-black edge
# tone (sg::edge_dark, slot 250) because the logo sits on the bright title art.
LOGO_INK=250
image=Image.open(ROOT/'assets/ui/silk-title-v1.png').convert('RGB')
image=image.resize((240,160),Image.LANCZOS).filter(ImageFilter.GaussianBlur(.25))
quantized=image.quantize(colors=48,method=Image.MEDIANCUT,dither=Image.NONE)
raw=quantized.getpalette()[:144]
title_rgb=[tuple(raw[i:i+3]) for i in range(0,144,3)]
colors=ui_rgb+title_rgb+map_rgb
(ROOT/'generated/ui_palette.inc').write_text('\n'.join('bn::color('+','.join(str(round(v*31/255)) for v in color)+'),' for color in colors)+'\n',encoding='ascii')
(ROOT/'generated/ui_extra_palette.inc').write_text('\n'.join('bn::color('+','.join(map(str,color))+'),' for color in tone_5bit)+'\n',encoding='ascii')
# Official Simplified Chinese face, shared with the legacy battle text path.
font_path=FONT_PATH
def font(size,weight=550,path=None):
    f=ImageFont.truetype(str(path or font_path),size)
    if hasattr(f,'set_variation_by_axes'):
        try: f.set_variation_by_axes([weight])
        except OSError: pass
    return f
source=''.join(p.read_text(encoding='utf-8') for p in (ROOT/'src').glob('*.cpp'))
source+=''.join(p.read_text(encoding='utf-8') for p in [ROOT/'generated/game_data.cpp'])
source+=''.join(bytes(int(v,16) for v in re.findall(r'\\x([0-9a-fA-F]{2})',escaped)).decode('utf-8',errors='replace') for escaped in re.findall(r'(?:\\x[0-9a-fA-F]{2})+',source))
# C++ universal character escapes are runtime text too, not ASCII source.
source+=''.join(chr(int(short or long,16)) for short,long in re.findall(r'\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})',source))
source+='… · /上下选择确认返回换将前军统领守城编组全部后列读档开始暂无物品资金城防人口'
# Private-use junk occurs only in a disabled, mojibake legacy model block.
characters=sorted({c for c in source if ord(c)>=32 and c!='\\' and not 0xE000<=ord(c)<=0xF8FF})
lines=['// Generated by tools/import_silk_ui.py. ZLabs Pixel 12px M CN;', '// provenance and fallback: assets/fonts/ZLabsPixel-SOURCE.md', '#include "ui_theme.h"','namespace sg {']
def emit_glyphs(name,size,chars):
    rows=[]
    for c in chars:
        width=(7 if size==12 else 9) if ord(c)<128 else size
        masks=raster(c,size,width)+[0]*(16-size)
        rows.append('{'+str(ord(c))+',{'+','.join(map(str,masks))+'},'+str(width)+'}')
    lines.append(f'const UiGlyph {name}[]={{'+','.join(rows)+'};')
    lines.append(f'const int {name[:-1]}_count={len(chars)};')
emit_glyphs('ui_glyphs',12,characters)
heading_chars=characters
emit_glyphs('heading_glyphs',16,heading_chars)
lines.append('alignas(4) const uint8_t silk_title_pixels[38400]={'+','.join(str(182+i) for i in quantized.getdata())+'};')
word=Image.new('1',(64,12),0)
for i,c in enumerate('三国群英传'):
    for y,bits in enumerate(raster(c,12,12)):
        for x in range(12):
            if bits&(1<<x): word.putpixel((i*13+x,y),1)
logo=Image.new('1',(144,32),0)
# An exact 2x enlargement keeps the title's square pixels.
logo.paste(word.resize((128,24),Image.NEAREST),(8,4))
pixels=[LOGO_INK if a else 0 for a in logo.getdata()]
lines.append('alignas(4) const uint8_t silk_logo_pixels[4608]={'+','.join(map(str,pixels))+'};')
lines.append('}')
(ROOT/'generated/silk_ui_data.cpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
preview=Image.new('RGB',(240,160)); preview.putdata([title_rgb[i] for i in quantized.getdata()])
preview.save(ROOT/'assets/ui/silk-title-gba.png')
print(f'Silk UI: {len(characters)} body glyphs, 48 title colors, 16 UI colors, 16 map colors')
if FALLBACK_GLYPHS: print('Legacy fallback glyphs: '+''.join(sorted(FALLBACK_GLYPHS)))
