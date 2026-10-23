"""Regenerate the main font subset without any external game inputs."""
from pathlib import Path
import re
from game_font import raster
ROOT=Path(__file__).resolve().parents[1]
characters={chr(int(cp)) for cp in re.findall(r'^\{(\d+),', (ROOT/'generated/font_data.cpp').read_text(encoding='utf-8'),re.M)}
for folder in ('src','include','generated'):
    for path in (ROOT/folder).glob('*'):
        if path.suffix in ('.cpp','.h') and path.name!='font_data.cpp':
            characters.update(path.read_text(encoding='utf-8'))
characters={c for c in characters if 32<=ord(c)<=126 or '\u3000'<=c<='\u9fff' or c in '，。！？：；（）《》【】、·—…'}
characters.update(chr(int(cp)) for cp in re.findall(r'^\{(\d+),', (ROOT/'generated/font_data.cpp').read_text(encoding='utf-8'),re.M))
lines=['// Generated from project text with ZLabs Pixel and Source Han Mono fallback.','#include "data.h"','namespace sg {','const Glyph glyphs[] = {']
for c in sorted(characters):
    width=6 if ord(c)<128 else 12
    rows=raster(c,12,width)
    lines.append('{'+str(ord(c))+',{'+','.join(map(str,rows))+'},'+str(width)+'},')
lines+=['};','const int glyph_count=sizeof(glyphs)/sizeof(glyphs[0]);','}']
# Preserve the existing Glyph initializer structure; see include/data.h.
(ROOT/'generated/font_data.cpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Generated',len(characters),'glyphs')
