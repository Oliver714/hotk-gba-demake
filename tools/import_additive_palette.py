"""Compile exact white-light palette matching to ROM, never during rendering."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/render.bn_iwram.cpp').read_text(encoding='utf-8')
body=source.split('const bn::color colors[256] = {',1)[1].split('};',1)[0]
body=re.sub(r'#include "([^"]+)"',lambda m:(ROOT/'generated'/m[1]).read_text(),body)
palette=[tuple(map(int,p)) for p in re.findall(r'bn::color\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)',body)]
assert 166<=len(palette)<=256
palette += [(0,0,0)]*(256-len(palette))
tables=[]
for half in (0,1):
    rows=[]
    for src in palette[118:166]:
        values=[]
        for dst in palette:
            rgb=tuple(min(31,(v*127//255 if half else v)+d) for v,d in zip(src,dst))
            values.append(min(range(166),key=lambda i:sum((a-b)**2 for a,b in zip(rgb,palette[i]))))
        rows.append('{'+','.join(map(str,values))+'}')
    tables.append('{'+','.join(rows)+'}')
(ROOT/'generated/additive_palette.cpp').write_text('// Generated; exact original nearest-palette result, including tie order.\n#include "additive_palette.h"\nnamespace sg {\nconst uint8_t additive_palette[2][48][256]={'+','.join(tables)+'};\n}\n',encoding='ascii')
print('Compiled 24576 exact additive palette entries into ROM')
