"""Generate lossless nontransparent row bounds for shared additive sprites."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
groups=[
    ('magic_m020_field','magic_m020_field_sprites',48,48),
    ('magic_healing','magic_healing_sprites',24,24),
    ('magic_dragon','magic_dragon_sprites',48,48),
    ('magic_dragon','magic_dragon_trails',32,48),
    ('magic_dragon','magic_dragon_streaks',48,32),
    ('magic_fire_hit','magic_fire_hit_sprites',32,40),
    ('magic_m013_cyclone','magic_m013_cyclone_sprites',48,24),
    ('magic_m013_cyclone','magic_m013_cyclone_b_sprites',48,24),
    ('magic_lightning','magic_lightning_sprites',44,96),
    ('magic_strikes','magic_strike_sprites',44,88),
    ('magic_m014_sparks','magic_m014_spark_sprites',16,16),
    ('magic_yellow_dragon','yellow_dragon_sprite',72,100),
    ('magic_yellow_dragon','yellow_dragon_orbs',24,24),
    ('magic_yellow_dragon','yellow_dragon_flame',32,112),
    ('magic_yellow_dragon','yellow_dragon_fire_0',10,44),
    ('magic_yellow_dragon','yellow_dragon_fire_1',16,72),
    ('magic_yellow_dragon','yellow_dragon_fire_2',22,98),
]
lines=['// Generated lossless row spans; no sprite pixels are changed.','#include "effect_spans.h"','#include <stdint.h>']
lines += [f'#include "{file}.h"' for file in dict.fromkeys(item[0] for item in groups)]
lines += ['namespace sg { namespace {']
lookups=[]
for file,name,w,h in groups:
    text=(ROOT/f'generated/{file}.cpp').read_text()
    match=re.search(r'const uint8_t '+name+r'((?:\[\d+\])+)\s*=\s*\{(.*?)\};',text,re.S)
    assert match,name
    dims=list(map(int,re.findall(r'\d+',match[1])))
    pixels=list(map(int,re.findall(r'\d+',match[2])))
    assert len(pixels)==__import__('math').prod(dims) and len(pixels)%(w*h)==0,name
    assert all(p==0 or 118<=p<166 for p in pixels),name
    rows=[]
    for start in range(0,len(pixels),w):
        nonzero=[i for i,p in enumerate(pixels[start:start+w]) if p]
        first,last=(nonzero[0],nonzero[-1]+1) if nonzero else (0,0)
        assert not any(pixels[start:start+first]) and not any(pixels[start+last:start+w])
        rows.extend((first,last))
    lines.append(f'const uint8_t rows_{name}[]={{'+','.join(map(str,rows))+'};')
    lookups += [f'    if(width=={w} && height=={h}) {{',
                f'        const uintptr_t offset=uintptr_t(pixels)-uintptr_t({name});',
                f'        if(offset<sizeof({name}) && offset%{w*h}==0) return rows_{name}+(offset/{w*h})*{h*2};','    }']
lines += ['}', 'const uint8_t* effect_sprite_rows(const uint8_t* pixels,int width,int height) {',*lookups,'    return nullptr;','}','}']
for path,content in ((ROOT/'generated/effect_spans.cpp','\n'.join(lines)+'\n'),
    (ROOT/'include/effect_spans.h','#pragma once\n#include <stdint.h>\nnamespace sg { const uint8_t* effect_sprite_rows(const uint8_t* pixels,int width,int height); }\n')):
    if not path.exists() or path.read_text()!=content:path.write_text(content,encoding='ascii')
print(f'Generated exact row bounds for {len(groups)} shared effect groups')
