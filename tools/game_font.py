"""Native ZLabs 12px masks; preserve existing cell metrics and runtime renderer."""
from functools import lru_cache
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = Path(__file__).resolve().parents[1] / 'assets/fonts/ZLabsPixel_12px_M_CN.ttf'
FALLBACK_PATH = FONT_PATH.with_name('SourceHanMonoSC-Regular.otf')
FALLBACK_GLYPHS = set()

@lru_cache(None)
def font(size):
    return ImageFont.truetype(str(FONT_PATH), size)

@lru_cache(None)
def load_font(path,size):
    return ImageFont.truetype(str(path),size)

def supported(character,path=FONT_PATH):
    face=load_font(path,12)
    def signature(c):
        mask=face.getmask(c,mode='1')
        return mask.size,bytes(mask)
    return character.isspace() or signature(character)!=signature('\U0010ffff')

def raster(character, size, width, path=None):
    selected=Path(path) if path else FONT_PATH
    if supported(character,selected):
        # Center native pixels in heading slots, never stretch 12px to16px.
        face=load_font(selected,12)
        offset_x=(width-12)//2 if width>=16 else (width-6)//2 if size==16 and ord(character)<128 else 0
        offset_y=(size-12)//2
        image=Image.new('1',(width,size))
        baseline=-face.getbbox('国',anchor='ls')[1]
        ImageDraw.Draw(image).text((offset_x,offset_y+baseline),character,font=face,fill=1,anchor='ls')
        return [sum(bool(image.getpixel((x,y)))<<x for x in range(width)) for y in range(size)]
    if selected!=FONT_PATH or not supported(character,FALLBACK_PATH):
        raise ValueError(f'Font has no glyph for {character!r} U+{ord(character):04X}')
    # Keep real glyphs for rare characters in scanned, disabled legacy strings.
    FALLBACK_GLYPHS.add(character)
    scale = 4
    face = load_font(FALLBACK_PATH,size*scale)
    if character != ' ' and bytes(face.getmask(character)) == bytes(face.getmask('\ue123')):
        raise ValueError(f'Font has no glyph for {character!r} U+{ord(character):04X}')
    # A shared baseline keeps sparse characters such as 一 in their proper row.
    baseline = -face.getbbox('国', anchor='ls')[1]
    natural_width = max(width*scale, round(face.getlength(character)))
    image = Image.new('L', (natural_width, size*scale))
    ImageDraw.Draw(image).text((0, baseline), character, font=face, fill=255, anchor='ls')
    # Source Han Mono's Latin advance is 2/3 em. Fit that entire cell rather
    # than clipping the right-hand stems into the legacy 6/7/9px slots.
    image = image.resize((width, size), Image.LANCZOS)
    return [sum((image.getpixel((x,y)) >= 112) << x for x in range(width)) for y in range(size)]
