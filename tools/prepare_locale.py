"""Generate an isolated English source/resource tree; never rewrite Chinese inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
from localization import ROOT, inventory, load_catalog, text_id, translate_cpp


def write_changed(path, value):
    content = value.encode('utf-8') if isinstance(value, str) else value
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)


def replace_once(source, before, after, label):
    if source.count(before) != 1:
        raise ValueError('English layout patch context changed: ' + label)
    return source.replace(before, after)


def prepare():
    catalog = load_catalog()
    strings = catalog['strings']
    rows = inventory()
    missing = [source for source in rows if source not in strings]
    if missing:
        raise ValueError('English catalog is incomplete: ' + repr(missing))
    stage = ROOT / '.cache/localization/en'
    source_hash = hashlib.sha256()
    test_strings = dict(strings, **{'%u年%u月': '%u/%u', '忠臣不事二主': 'Still loyal'})
    for year, month in ((189,1), (1000,12), (65535,12), (0,0)):
        test_strings[f'{year}年{month}月'] = f'{year}/{month}'
    for folder in ('src', 'include', 'generated', 'tests'):
        for file in sorted((ROOT / folder).iterdir()):
            if not file.is_file() or file.suffix not in ('.cpp', '.h', '.c', '.s', '.inc', '.adpcm'):
                continue
            content = file.read_bytes()
            source_hash.update(file.relative_to(ROOT).as_posix().encode() + b'\0' + content)
            if file.suffix in ('.cpp', '.h', '.c'):
                content, _ = translate_cpp(content.decode('utf-8'), test_strings if folder == 'tests' else strings,
                                           strict=folder != 'tests')
            write_changed(stage / folder / file.name, content)
    write_changed(stage / 'Makefile', (ROOT / 'Makefile').read_bytes())
    vendor = stage / 'vendor'
    if os.name != 'nt' and not vendor.exists():
        vendor.symlink_to(ROOT / 'vendor', target_is_directory=True)

    font_source = (ROOT / 'generated/font_data.cpp').read_text(encoding='utf-8')
    glyphs = {}
    for cp, bitmap, width in re.findall(r'\{(\d+),\{([^}]+)\},(\d+)\}', font_source):
        if 32 <= int(cp) <= 126:
            glyphs[int(cp)] = ([int(v) for v in bitmap.split(',')], int(width))
    if len(glyphs) != 95:
        # These four ASCII characters are absent from the Chinese subset.
        # Native 5x8 masks keep builds independent of host font libraries.
        extra = {36: [4,30,5,14,20,15,4,0],
                 64: [14,17,29,21,29,1,14,0],
                 74: [28,8,8,8,8,9,6,0],
                 92: [1,1,2,2,4,4,8,8]}
        for cp, bitmap in extra.items():
            if cp not in glyphs:
                glyphs[cp] = ([0,0] + bitmap + [0,0], 6)
    if len(glyphs) != 95:
        raise ValueError('The Latin pixel subset must contain printable ASCII')
    font = ['// English ASCII subset of the licensed ZLabs pixel glyphs.', '#include "data.h"', 'namespace sg {', 'const Glyph glyphs[]={']
    for cp, (bitmap, width) in sorted(glyphs.items()):
        font.append('{' + str(cp) + ',{' + ','.join(map(str, bitmap)) + '},' + str(width) + '},')
    font += ['};', 'const int glyph_count=95;', '}']
    write_changed(stage / 'generated/font_data.cpp', '\n'.join(font) + '\n')

    skin = (ROOT / 'generated/silk_ui_data.cpp').read_text(encoding='utf-8')
    for name, large in [('ui_glyphs', False), ('heading_glyphs', True)]:
        values = []
        for cp, (bitmap, _) in sorted(glyphs.items()):
            bitmap = ([0, 0] + bitmap + [0, 0]) if large else bitmap + [0] * 4
            values.append('{' + str(cp) + ',{' + ','.join(map(str, bitmap)) + '},6}')
        pattern = r'const UiGlyph ' + name + r'\[\]=\{.*?\};'
        skin, count = re.subn(pattern, 'const UiGlyph ' + name + '[]={' + ','.join(values) + '};', skin, flags=re.S)
        if count != 1:
            raise ValueError('UI glyph table changed: ' + name)
        skin = re.sub(r'const int ' + name[:-1] + r'_count=\d+;', 'const int ' + name[:-1] + '_count=95;', skin)
    # Two 5x7 uppercase lines at exact 2x scale, in the existing logo rectangle.
    logo = [0] * (144 * 32)
    for line, text in enumerate(('THREE', 'KINGDOMS')):
        left = (144 - len(text) * 12 + 2) // 2
        for index, char in enumerate(text):
            bitmap = glyphs[ord(char)][0][2:10]
            bitmap = bitmap[:4] + bitmap[5:]  # compact the 8px capital to 7 rows
            for y, bits in enumerate(bitmap):
                for x in range(5):
                    if bits & (1 << x):
                        for dy in (0, 1):
                            for dx in (0, 1):
                                logo[(line * 16 + y * 2 + dy) * 144 + left + index * 12 + x * 2 + dx] = 250
    skin, count = re.subn(r'(const uint8_t silk_logo_pixels\[4608\]=)\{.*?\};',
                         lambda match: match[1] + '{' + ','.join(map(str, logo)) + '};', skin, flags=re.S)
    if count != 1:
        raise ValueError('Title logo table changed')
    write_changed(stage / 'generated/silk_ui_data.cpp', skin)

    # Short combat names occupy at most the original 72px label area.
    data_source = (stage / 'generated/game_data.cpp').read_text(encoding='utf-8')
    skill_body = re.search(r'const SkillDef skill_defs\[skill_count\] = \{(.*?)\n\};', data_source, re.S)[1]
    skill_names = re.findall(r'^\{"([^"\\]*)",', skill_body, re.M)
    if len(skill_names) != 50:
        raise ValueError('Expected 50 skill definitions')
    short_names = [catalog['short_skills'].get(name, name) for name in skill_names]
    if any(len(name) > 12 for name in short_names):
        raise ValueError('Combat skill alias too long: ' + repr([name for name in short_names if len(name) > 12]))
    helper = '#pragma once\n#include "data.h"\nnamespace sg {\n'
    helper += '[[gnu::noinline]] inline const char* english_skill_hud(const SkillDef* skill) {\n'
    helper += 'static const char* const names[50]={' + ','.join(json.dumps(name) for name in short_names) + '};\n'
    helper += 'return skill ? names[skill-skill_defs] : "--";\n}\n'
    helper += '''inline const char* english_compact_skill(const SkillDef* skill) {
    const char* name=english_skill_hud(skill); const volatile char* scan=name;
    int length=0;while(scan[length])++length;
    if(length<=10)return name;
    static char compact[12];for(int i=0;i<9;++i)compact[i]=name[i];compact[9]='.';compact[10]=0;
    return compact;
}
'''
    short_roles = catalog['short_roles']
    titles = re.findall(r'^\{"[^"\\]*","([^"\\]*)",', skill_body, re.M)
    missing_roles = [title for title in titles if len(title)>7 and title not in short_roles]
    if missing_roles:
        raise ValueError('Missing compact English rank title: ' + repr(missing_roles))
    if any(not name.isascii() or len(name)>7 for name in short_roles.values()):
        raise ValueError('Compact English rank title exceeds its field')
    helper += 'inline const char* english_compact_role(const char* name) {\n'
    helper += 'static const char* const full[]={' + ','.join(json.dumps(n) for n in short_roles) + '};\n'
    helper += 'static const char* const brief[]={' + ','.join(json.dumps(n) for n in short_roles.values()) + '};\n'
    helper += 'for(int i=0;i<' + str(len(short_roles)) + ';++i){const char* a=name;const char* b=full[i];while(*a && *a==*b){++a;++b;}if(*a==*b)return brief[i];}\nreturn name;\n}\n'
    # Two compact initials, each 3x5 pixels, fit inside the existing city flag.
    letters = ['111101111101101','110101110101110','111100100100111','110101101101110','111100110100111','111100110100100',
               '111100101101111','101101111101101','111010010010111','001001001101111','101101110101101','100100100100111',
               '101111111101101','101111111111101','111101101101111','111101111100100','111101101111001','110101110101101',
               '111100111001111','111010010010010','101101101101111','101101101101010','101101111111101','101101010101101',
               '101101010010010','111001010100111']
    helper += 'inline unsigned english_flag_row(const char* name,int row) {\n'
    helper += 'static const unsigned short letters[26]={' + ','.join(str(int(bits, 2)) for bits in letters) + '};\n'
    helper += 'char first=*name,second=0;const char* p=name;while(*p && *p!=\' \')++p;second=*p ? p[1] : name[1];\n'
    helper += "if(name[0]=='L' && p[0]==' ' && p[1]=='B')second=p[2]=='i'?'I':'B';\n"
    helper += "if(name[0]=='L' && p[0]==' ' && p[1]=='Y')second=p[2]=='n'?'N':'O';\n"
    helper += "if(name[0]=='Y' && p[0]==' ' && p[1]=='S')second=p[2]=='a'?'S':'U';\n"
    helper += 'auto pixels=[&](char c){if(c>=\'a\' && c<=\'z\')c-=32;return c>=\'A\' && c<=\'Z\' ? unsigned(letters[c-\'A\']>>(12-row*3))&7u : 0u;};\n'
    helper += 'return (pixels(first)<<4)|pixels(second);\n}\n'
    helper += '''inline bool english_same(const char* a,const char* b) {
    while(*a && *a==*b) { ++a; ++b; } return *a==*b;
}
inline const char* english_compact_general(int id) {
    const char* name=general_defs[id].name; int size=0; while(name[size])++size;
    if(size<=8)return name;
    static char out[16]; int split=-1; for(int i=0;i<size;++i)if(name[i]==' ')split=i;
    if(split<0) { for(int i=0;i<7;++i)out[i]=name[i];out[7]='.';out[8]=0;return out; }
    if(split<=4) {
        for(int i=0;i<split;++i)out[i]=name[i];
        out[split]=' ';out[split+1]=name[split+1];out[split+2]='.';out[split+3]=0;
        return out;
    }
    out[0]=name[0];out[1]='.';out[2]=' ';
    int n=0;while(n<5 && name[split+1+n]) { out[3+n]=name[split+1+n];++n; }
    out[3+n]=0;
    return out;
}
inline const char* english_compact_king(const char* name) {
    static const char* const full[]={"Sun Jian","Dong Zhuo","Ding Yuan","Gongsun Zan","Zhang Jiao","Yuan Shao","Yuan Shu","Liu Biao","Yan Baihu","Wang Lang","Liu Zhang","Gong Jing","Qiao Xuan","Tao Qian","Kong Rong","Kong Xiu","Qiao Mao","Jin Xuan","Meng Huo","Sun Quan","Zhang Lu"};
    static const char* const short_name[]={"Sun J.","D. Zhuo","D. Yuan","G. Zan","Z. Jiao","Y. Shao","Y. Shu","L. Biao","Y. Bai","W. Lang","L.Zhang","G. Jing","Q. Xuan","T. Qian","K. Rong","K. Xiu","Q. Mao","J. Xuan","M. Huo","Sun Q.","Z. Lu"};
    for(int i=0;i<21;++i)if(english_same(name,full[i]))return short_name[i];
    return name;
}
inline const char* english_compact_force(int force) {
    static const char* const names[]={"Sword","Spear","Bow","Glaive","Knife","Flail","Rebel","Tribal","Rattan","Fight","Hammer"};
    return names[force<0?0:force>10?10:force];
}
inline const char* english_compact_formation(int formation) {
    static const char* const names[]={"Square","Circle","Wedge","Geese","Mystic","Fish","Hook"};
    return names[formation<0?0:formation>6?6:formation];
}
inline const char* english_compact_city(int city) {
    const char* name=city_defs[city].name;
    static const char* const full[]={"Xiangping","Xiangyang","Shangyong","Jiangling","Jiangzhou","Pingyuan","Hongnong","Chang'an","Tianshui","Shouchun","Hanzhong","Jianning","Changsha","Jiangxia","Lingling"};
    static const char* const short_name[]={"Xiangp.","Xiangy.","Shangy.","Jiangl.","Jiangz.","Pingy.","Hongn.","Chang'n","Tiansh.","Shouch.","Hanzh.","Jiann.","Changs.","Jiangx.","Lingl."};
    for(int i=0;i<15;++i)if(english_same(name,full[i]))return short_name[i];
    return name;
}
inline const char* english_compact_equipped_item(int item,const char* name) {
    return item==41 ? "Flying Light" : name;
}
}\n'''
    write_changed(stage / 'include/english_ui.h', helper)

    file = stage / 'src/render.bn_iwram.cpp'
    source = file.read_text(encoding='utf-8')
    source = '#include "english_ui.h"\n' + source
    source = replace_once(source, 'skill ? skill->name : "--"', 'english_skill_hud(skill)', 'combat skill alias')
    source = replace_once(source, 'text(110,146,"T",muted_ink);',
                          'text(110,146,"s",muted_ink);', 'battle timer label')
    # The Chinese header was tightly packed for short labels. Widen English
    # field spacing while keeping both 110px faction columns inside 240px.
    source = replace_once(source, 'rect(x+13,24,18,12,paper);\n        number(x+13,24,fighter.hp,',
                          'rect(x+15,24,28,12,paper);\n        number(x+15,24,fighter.hp,', 'battle HP value')
    source = replace_once(source, 'rect(x+51,24,18,12,paper); number(x+51,24,fighter.mp,',
                          'rect(x+59,24,21,12,paper); number(x+59,24,fighter.mp,', 'battle MP value')
    source = replace_once(source, 'rect(x+85,24,18,12,paper); number(x+85,24,fighter.soldiers,',
                          'rect(x+88,24,21,12,paper); number(x+88,24,fighter.soldiers,', 'battle soldier count')
    source = replace_once(source, 'ui_text(5,first_y,city_defs[selected].name,ochre,39);',
                          'ui_text(5,first_y,city_defs[selected].name,ochre,78);', 'map city name')
    source = replace_once(source, 'ui_text(47,first_y,"Of",muted_ink,13);', '', 'map owner prefix')
    source = replace_once(source, 'rect(60,first_y+4,4,4,king_ink(c.king,game.campaign.player));',
                          'rect(88,first_y+4,4,4,king_ink(c.king,game.campaign.player));', 'map owner swatch')
    source = replace_once(source, 'ui_text(67,first_y,c.king >= 0 ? king_defs[game.campaign.scenario][c.king].name : "Neutral",king_ink(c.king,game.campaign.player),60);',
                          'ui_text(97,first_y,c.king >= 0 ? king_defs[game.campaign.scenario][c.king].name : "Neutral",king_ink(c.king,game.campaign.player),91);', 'map owner name')
    source = replace_once(source, 'ui_text(132,first_y,c.king<0 ? "Neutral" : c.king==game.campaign.player ? "Your city" : "Other faction",c.king==game.campaign.player ? indigo : c.king<0 ? muted_ink : cinnabar,102);',
                          'ui_text(197,first_y,c.king<0 ? "Free" : c.king==game.campaign.player ? "Own" : "Enemy",c.king==game.campaign.player ? indigo : c.king<0 ? muted_ink : cinnabar,38);', 'map relation')
    source = replace_once(source, 'ui_number(89,second_y,c.defense);', 'ui_number(96,second_y,c.defense);', 'map defense')
    write_changed(file, source)
    file = stage / 'src/main.cpp'
    source = file.read_text(encoding='utf-8')
    source = '#include "english_ui.h"\n' + source
    source = replace_once(source, 'render.ui_text(94,117,"Officer",sg::muted_ink);',
                          'render.ui_text(94,117,"By",sg::muted_ink);', 'city worker')
    source = replace_once(source, 'render.ui_text(120,138,"A/Start:Continue",sg::muted_ink,108);',
                          'render.ui_text(120,138,"A/Start:Next",sg::muted_ink,108);', 'year end prompt')
    # Tight fixed-width English labels in general, save, route and result panels.
    compact_labels = {
        '"Faction",sg::muted_ink,28': '"Side",sg::muted_ink,28',
        '"Level",sg::muted_ink,28': '"Lv",sg::muted_ink,28',
        '"Loyalty",sg::muted_ink,28': '"LOY",sg::muted_ink,28',
        '"Troops",sg::muted_ink,32': '"Trps",sg::muted_ink,32',
        '"Formation",sg::muted_ink,60': '"Form",sg::muted_ink,60',
        '"Current",sg::muted_ink,40': '"Now",sg::muted_ink,40',
        '"Current",sg::cinnabar,40': '"Now",sg::cinnabar,40',
        '"Troop Type",sg::muted_ink,60': '"Type",sg::muted_ink,60',
        '"Slot",sg::muted_ink,27': '"No.",sg::muted_ink,27',
        '"Cities",sg::muted_ink,27': '"Cty",sg::muted_ink,27',
        '"Officers",sg::muted_ink,27': '"Gen.",sg::muted_ink,27',
        '"Owner",sg::muted_ink,28': '"Own",sg::muted_ink,28',
        '"Target",sg::muted_ink,27': '"To",sg::muted_ink,27',
        '"None",sg::muted_ink,13': '"--",sg::muted_ink,13',
        '"Garrison",sg::muted_ink,28': '"Hold",sg::muted_ink,28',
        '"Troops",sg::muted_ink,28': '"Trps",sg::muted_ink,28',
        '"Defense",sg::muted_ink,28': '"DF",sg::muted_ink,28',
        '"Reserve",sg::muted_ink,41': '"Res.",sg::muted_ink,41',
        '"Done",sg::muted_ink,14': '"OK",sg::muted_ink,14',
        '"Population gain",sg::muted_ink,61': '"Pop gain",sg::muted_ink,61',
        '"Population now",sg::muted_ink,61': '"Pop now",sg::muted_ink,61',
        '"Defense gain",sg::muted_ink,61': '"DF gain",sg::muted_ink,61',
        '"Defense now",sg::muted_ink,61': '"DF now",sg::muted_ink,61',
        '"Item found",sg::muted_ink,58': '"Found",sg::muted_ink,58',
        '"Stored in city inventory",sg::muted_ink,205': '"Stored in city inventory",sg::muted_ink,205',
        '"Choose a target after assembly",sg::muted_ink,153': '"Choose target city",sg::muted_ink,153',
        '"Officer",sg::muted_ink,30': '"Gen.",sg::muted_ink,30',
        '"Route",sg::muted_ink,30': '"Path",sg::muted_ink,30',
        '"Remaining",sg::muted_ink,34': '"Left",sg::muted_ink,34',
        '"Ready",sg::muted_ink,27': '"Set",sg::muted_ink,27',
        '"Winner",sg::ochre,40': '"Win",sg::ochre,40',
        '"Captured",sg::muted_ink,28': '"Capt.",sg::muted_ink,28',
        '"Your Officers",sg::muted_ink,70': '"Officers",sg::muted_ink,70',
        '"Status",sg::muted_ink,40': '"State",sg::muted_ink,40',
        '"Destination / Faction",sg::muted_ink,80': '"To / Side",sg::muted_ink,80',
        '"United",sg::muted_ink,26': '"Won",sg::muted_ink,26',
    }
    for old, new in compact_labels.items():
        source = source.replace(old, new)
    source = replace_once(source, '"has united China."',
                          '"has united China."', 'victory text')
    source = replace_once(source, '"The dream of one China is fulfilled"',
                          '"One realm at last"', 'ending text')
    for old, new, label in [
        ('render.ui_text(70,133,army.garrisoned ? "Stationed" : army.retreating ? "Retreat" : "Marching",sg::muted_ink,50);',
         'render.ui_text(70,133,army.garrisoned ? "Camp" : army.retreating ? "Retreat" : "March",sg::muted_ink,50);', 'map focused army status'),
        ('"You can select a slot"', '"Select a slot"', 'slot hint'),
        ('"Done",sg::disabled_ink,14', '"OK",sg::disabled_ink,14', 'disabled done label'),
        ('"Equip",sg::muted_ink,26', '"Eq.",sg::muted_ink,26', 'equipment label'),
        ('"LOY",sg::muted_ink,12', '"L",sg::muted_ink,12', 'prisoner loyalty label'),
        ('"LOY",sg::muted_ink,13', '"L",sg::muted_ink,13', 'persuasion loyalty label'),
        ('"Level",sg::muted_ink,28', '"Lv",sg::muted_ink,28', 'profile level label'),
        ('"Loyalty",sg::muted_ink,28', '"LOY",sg::muted_ink,28', 'profile loyalty label'),
        ('"Level",sg::muted_ink,30', '"Lv",sg::muted_ink,30', 'prisoner level label'),
        ('"Loyalty",sg::muted_ink,30', '"LOY",sg::muted_ink,30', 'prisoner loyalty detail'),
        ('"Troops",sg::muted_ink,30', '"Trps",sg::muted_ink,30', 'prisoner troop label'),
        ('"Faction",sg::muted_ink,32', '"Side",sg::muted_ink,32', 'army faction label'),
        ('"Tr",sg::muted_ink,13', '"#",sg::muted_ink,13', 'army troop count label'),
        ('"Ready",sg::cinnabar,27', '"Set",sg::cinnabar,27', 'formation status label'),
        ('"Captured",sg::cinnabar,28', '"POW",sg::cinnabar,28', 'captured heading'),
        ('"United",sg::gold_hi', '"Win",sg::gold_hi', 'ending seal'),
        ('"The city will be unclaimed."', '"City becomes unclaimed."', 'abandon city notice'),
        ('"Choose an officer to stay as governor."', '"Choose a governor."', 'prefect prompt'),
    ]:
        if old in source:
            source = source.replace(old, new)
        elif new not in source:
            raise ValueError('English compact label context changed: ' + label)
    # Round two: additional fixed-column issues found on army and battle pages.
    for old, new, label in [
        ('render.row(y,commander>=0 ? sg::general_defs[commander].name : "No officer",i==army_cursor);',
         'render.row(y,commander>=0 ? sg::english_compact_general(commander) : "No officer",i==army_cursor);', 'army list commander'),
        ('army.king>=0 ? sg::king_defs[game.campaign.scenario][army.king].name : "Neutral",',
         'army.king>=0 ? sg::english_compact_king(sg::king_defs[game.campaign.scenario][army.king].name) : "Neutral",', 'army list faction'),
        ('army.garrisoned ? "Stationed" : army.retreating ? "Retreat" : "Marching",\n                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,36);',
         'army.garrisoned ? "Camp" : army.retreating ? "Retreat" : "March",\n                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,36);', 'army list status'),
        ('army.garrisoned ? "Stationed" : army.retreating ? "Retreat" : "Marching",\n                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,50);',
         'army.garrisoned ? "Camp" : army.retreating ? "Retreat" : "March",\n                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,50);', 'army detail status'),
        ('render.ui_text(10,43,sg::city_defs[army.origin].name,sg::text_ink,53);',
         'render.ui_text(10,43,sg::english_compact_city(army.origin),sg::text_ink,53);', 'army origin city'),
        ('render.ui_text(93,43,sg::city_defs[sg::army_destination(game.campaign,army_indices[army_cursor])].name,sg::text_ink,53);',
         'render.ui_text(93,43,sg::english_compact_city(sg::army_destination(game.campaign,army_indices[army_cursor])),sg::text_ink,53);', 'army target city'),
        ('render.ui_text(31,y,sg::general_defs[general].name,sg::text_ink,61);',
         'render.ui_text(31,y,sg::english_compact_general(general),sg::text_ink,61);', 'army officer name'),
        ('render.ui_text(12,y,sg::general_defs[g].name,sg::text_ink,61);',
         'render.ui_text(12,y,sg::english_compact_general(g),sg::text_ink,61);', 'roster officer name'),
        ('render.ui_text(12,y,sg::general_defs[general].name,ink,82);',
         'render.ui_text(12,y,sg::english_compact_general(general),ink,82);', 'domestic officer name'),
        ('render.ui_text(24,y,sg::general_defs[general].name,sg::text_ink,84);',
         'render.ui_text(24,y,sg::english_compact_general(general),sg::text_ink,84);', 'prisoner officer name'),
        ('render.ui_text(x+4,y,sg::general_defs[side==0 ? game.battle.attackers[i] : game.battle.defenders[i]].name,',
         'render.ui_text(x+4,y,sg::english_compact_general(side==0 ? game.battle.attackers[i] : game.battle.defenders[i]),', 'battle roster officer name'),
        ('render.ui_text(165,y,sg::ui_force_name(o.force),sg::muted_ink,40);',
         'render.ui_text(165,y,sg::english_compact_force(o.force),sg::muted_ink,44);', 'roster troop type'),
        ('side==0 ? "Attack" : "Defend",focused ? sg::cinnabar : sg::muted_ink,28);',
         'side==0 ? "Atk" : "Def",focused ? sg::cinnabar : sg::muted_ink,28);', 'battle side header'),
        ('game.battle.layout_front[side][i] ? "Front" : "Rear",sg::muted_ink,27);',
         'game.battle.layout_front[side][i] ? "F" : "R",sg::muted_ink,27);', 'battle line marker'),
        ('render.ui_text(x+4,119,sg::ui_force_name(officer.force),sg::text_ink,41);',
         'render.ui_text(x+4,119,sg::english_compact_force(officer.force),sg::text_ink,41);', 'battle troop type'),
        ('render.ui_text(x+47,119,sg::ui_formation_name(officer.formation),sg::muted_ink,43);',
         'render.ui_text(x+47,119,sg::english_compact_formation(officer.formation),sg::muted_ink,43);', 'battle formation'),
        ('render.ui_text(51,48,"Level",sg::muted_ink); render.ui_number(80,48,game.level(g));',
         'render.ui_text(51,48,"Lv",sg::muted_ink); render.ui_number(80,48,game.level(g));', 'profile level overlap'),
        ('render.ui_text(123,48,"Loyalty",sg::muted_ink); render.ui_number(153,48,o.loyalty);',
         'render.ui_text(123,48,"LOY",sg::muted_ink); render.ui_number(153,48,o.loyalty);', 'profile loyalty overlap'),
        ('render.ui_text(122,69,"Troops",sg::muted_ink); render.ui_number(158,69,o.soldiers);',
         'render.ui_text(122,69,"Trps",sg::muted_ink); render.ui_number(158,69,o.soldiers);', 'profile troops overlap'),
        ('render.panel(items_global_roster ? "Faction Items" : "City Items");',
         'render.panel("Items");', 'item header overlap'),
        ('render.ui_text(90,5,"Eq.",sg::muted_ink,26);',
         'render.ui_text(60,5,"Eq.",sg::muted_ink,26);', 'equipped header position'),
        ('render.ui_text(119,5,equipped ? equipped : "Unknown gear",sg::text_ink,76);',
         'render.ui_text(88,5,sg::english_compact_equipped_item(o.equipped_item,equipped ? equipped : "Unknown gear"),sg::text_ink,103);', 'equipped item width'),
        ('game.campaign.officers[general].city>=0 ? "Here" : "Army",sg::muted_ink,24);',
         'game.campaign.officers[general].city>=0 ? "At" : "Army",sg::muted_ink,36);', 'locator status width'),
        ('render.ui_text(137,y,sg::city_defs[location].name,sg::text_ink,86);',
         'render.ui_text(150,y,sg::city_defs[location].name,sg::text_ink,82);', 'locator city position'),
        ('c.king>=0 ? "Other faction" : "Neutral city",sg::muted_ink,73);',
         'c.king>=0 ? "Other side" : "Neutral city",sg::muted_ink,73);', 'city intel affiliation'),
        ('render.ui_text(143,28,"Selected",sg::muted_ink); render.ui_number(175,28,group_count);',
         'render.ui_text(143,28,"Sel",sg::muted_ink); render.ui_number(175,28,group_count);', 'army assembly selection count'),
        ('picked ? "Selected" : game.campaign.officers[g].hp ? "Available" : "Recovering",',
         'picked ? "Chosen" : game.campaign.officers[g].hp ? "Ready" : "Resting",', 'army assembly availability'),
        ('render.ui_text(210,27,"Tr",sg::muted_ink);',
         'render.ui_text(210,27,"#",sg::muted_ink);', 'officer roster troop count header'),
        ('render.ui_text(101,y,"Tr",sg::muted_ink);',
         'render.ui_text(101,y,"#",sg::muted_ink);', 'assembly troop count header'),
    ]:
        if old not in source and new not in source:
            raise ValueError('English round-two layout context changed: ' + label)
        source = source.replace(old,new)
    for old,new,label in [
        ('"Unlocks this troop type"','"Unlocks troop type"','item troop description'),
        ('"Unlocks this formation"','"Unlocks formation"','item formation description'),
        ('"Faster attacks while moving"','"Faster attacks while moving"','item movement bonus'),
        ('"Special equipment effect"','"Special gear effect"','item special effect'),
    ]:
        if old not in source and new not in source:
            raise ValueError('English round-three item text context changed: '+label)
        source=source.replace(old,new)
    # Round four: combat result and personnel panels contain dense columns that
    # are not exercised by the first three audits. Keep every English value
    # inside its actual pixel field.
    for old,new,label in [
        ('render.ui_text(148,28,"To / Side",sg::muted_ink,80);',
         'render.ui_text(148,28,"Where",sg::muted_ink,80);', 'battle report location header'),
        ('static const char* outcomes[]={"Garrisoned","Army","Retreat","Captured","Scattered","Rescued"};',
         'static const char* outcomes[]={"In City","March","Retreat","Captive","Missing","Rescued"};', 'battle report outcomes'),
        ('render.row(y,sg::general_defs[entry.general].name,cursor==i);',
         'render.row(y,sg::english_compact_general(entry.general),cursor==i);', 'battle report officer names'),
        ('entry.city>=0 && entry.city<sg::city_count ? sg::city_defs[entry.city].name : "En route";',
         'entry.city>=0 && entry.city<sg::city_count ? sg::english_compact_city(entry.city) : "En route";', 'battle report city names'),
        ('sg::king_defs[game.campaign.scenario][entry.king].name :',
         'sg::english_compact_king(sg::king_defs[game.campaign.scenario][entry.king].name) :', 'battle report faction names'),
        ('render.ui_text(181,47,"Level",sg::muted_ink);',
         'render.ui_text(181,47,"Lv",sg::muted_ink,24);', 'battle dossier level label'),
        ('render.ui_text(153,63,"Troops",sg::muted_ink); render.ui_number(190,63,officer.soldiers);',
         'render.ui_text(153,63,"Trps",sg::muted_ink,34); render.ui_number(190,63,officer.soldiers);', 'battle dossier troop label'),
        ('render.ui_text(10,116,"Known Skills",sg::muted_ink);',
         'render.ui_text(10,116,"Skills",sg::muted_ink,52);', 'battle dossier skill header'),
        ('render.heading(51,29,sg::general_defs[g].name,140);',
         'render.heading(51,29,sg::english_compact_general(g),140);', 'battle dossier officer name'),
        ('render.ui_text(87+(i%2)*75,116+(i/2)*12,skill->name,sg::text_ink,72);',
         'render.ui_text(87+(i%2)*75,116+(i/2)*12,sg::english_compact_skill(skill),sg::text_ink,72);', 'battle dossier skill names'),
        ('render.ui_text(9,70,"Total Troops",sg::muted_ink); render.ui_number(64,70,o.soldiers);',
         'render.ui_text(9,70,"Troops",sg::muted_ink,48); render.ui_number(64,70,o.soldiers);', 'troop total label'),
        ('render.ui_text(x,y,sg::ui_force_name(i),\n                           officer.force==i ? sg::cinnabar : known ? sg::text_ink : sg::muted_ink,70);',
         'render.ui_text(x,y,sg::english_compact_force(i),\n                           officer.force==i ? sg::cinnabar : known ? sg::text_ink : sg::muted_ink,70);', 'personnel troop type names'),
        ('render.ui_text(x+76,y,known ? "Known" : "Unknown",known ? sg::text_ink : sg::muted_ink,36);',
         'render.ui_text(x+76,y,known ? "Yes" : "No",known ? sg::text_ink : sg::muted_ink,36);', 'personnel troop availability'),
    ]:
        if old not in source and new not in source:
            raise ValueError('English round-four layout context changed: ' + label)
        source=source.replace(old,new,1)
    for old,new,label in [
        ('render.heading(53,49,sg::general_defs[winner].name,67);',
         'render.heading(53,49,sg::english_compact_general(winner),67);', 'duel winner name'),
        ('render.heading(176,49,sg::general_defs[loser].name,58);',
         'render.heading(176,49,sg::english_compact_general(loser),58);', 'duel loser name'),
        ('battle.round_result_kind==2 ? "Captured" :\n                battle.round_result_kind==4 ? "Retreat" : "Defeated";',
         'battle.round_result_kind==2 ? "Captive" :\n                battle.round_result_kind==4 ? "Retreat" : "Defeat";', 'duel outcome label'),
        ('before && after>before ? "Level Up" : "Level",sg::ochre,28',
         'before && after>before ? "Lv+" : "Lv",sg::ochre,28', 'duel level label'),
        ('"Officer Details","Officer Position","Withdraw from Battle"',
         '"Officer Details","Officer Position","Withdraw"', 'battle setup withdraw action'),
        ('"Promotion Complete"', '"Complete"', 'promotion complete label'),
        ('render.ui_text(10,27,sg::general_defs[general].name,sg::muted_ink,78);',
         'render.ui_text(10,27,sg::english_compact_general(general),sg::muted_ink,78);', 'troop selector officer name'),
        ('render.ui_text(32,91,sg::king_defs[game.campaign.scenario][game.campaign.player].name,sg::cinnabar,48);',
         'render.ui_text(32,91,sg::english_compact_king(sg::king_defs[game.campaign.scenario][game.campaign.player].name),sg::cinnabar,48);', 'ending ruler name'),
        ('render.ui_text(124,119,sg::ui_force_name(officer.force),sg::text_ink,55);',
         'render.ui_text(124,119,sg::english_compact_force(officer.force),sg::text_ink,55);', 'prisoner dossier troop type'),
        ('render.ui_text(180,119,sg::ui_formation_name(officer.formation),sg::muted_ink,54);',
         'render.ui_text(180,119,sg::english_compact_formation(officer.formation),sg::muted_ink,54);', 'prisoner dossier formation'),
        ('const char* owner=show_owner ? sg::king_defs[game.campaign.scenario][owner_king].name : "---";',
         'const char* owner=show_owner ? sg::english_compact_king(sg::king_defs[game.campaign.scenario][owner_king].name) : "---";', 'personnel dossier ruler name'),
        ('show_job ? job->title : "---",sg::text_ink,54',
         'show_job ? sg::english_compact_role(job->title) : "---",sg::text_ink,54', 'personnel dossier rank name'),
        ('render.ui_text(15,y,skill->title,sg::ochre,58);',
         'render.ui_text(15,y,sg::english_compact_role(skill->title),sg::ochre,58);', 'personnel skill title'),
        ('render.ui_text(76,y,skill->name,sg::text_ink,105);',
         'render.ui_text(76,y,sg::english_compact_skill(skill),sg::text_ink,105);', 'personnel skill name'),
    ]:
        if old not in source and new not in source:
            raise ValueError('English round-five layout context changed: ' + label)
        source=source.replace(old,new,1)
    # Recruit uses full English labels: align both numeric columns and reserve
    # a separate field for the formation instead of retaining Chinese offsets.
    source = replace_once(source,
        '''            render.ui_text(9,70,"Troops",sg::muted_ink,48); render.ui_number(64,70,o.soldiers);
            render.ui_text(124,70,"Reserve",sg::muted_ink); render.ui_number(180,70,game.campaign.cities[city].reserve);
            render.ui_text(9,85,"Regular",sg::muted_ink); render.ui_number(45,85,o.front);
            render.ui_text(124,85,"Elite",sg::muted_ink); render.ui_number(160,85,int(o.soldiers)-o.front);
            render.ui_text(9,100,"Formation",sg::muted_ink); render.ui_text(45,100,sg::ui_formation_name(o.formation),sg::text_ink,177);''',
        '''            render.ui_text(9,70,"Troops",sg::muted_ink,48); render.ui_number(72,70,o.soldiers);
            render.ui_text(124,70,"Reserve",sg::muted_ink,55); render.ui_number(184,70,game.campaign.cities[city].reserve);
            render.ui_text(9,85,"Regular",sg::muted_ink,48); render.ui_number(72,85,o.front);
            render.ui_text(124,85,"Elite",sg::muted_ink,55); render.ui_number(184,85,int(o.soldiers)-o.front);
            render.ui_text(9,100,"Form",sg::muted_ink,48); render.ui_text(72,100,sg::ui_formation_name(o.formation),sg::text_ink,158);''',
        'recruit label and value columns')
    source = replace_once(source,
        'render.button(x+(i%columns)*gap,y+(i/columns)*row_gap,width,pause_action(i),cursor==i,ink);',
        '''if(columns==3) {
                const int bx=x+(i%columns)*gap,by=y+(i/columns)*row_gap;
                render.ui_box(bx,by,width,19,cursor==i);
                render.ui_text(bx+6,by+3,pause_action(i),ink,width-12);
            } else render.button(x+(i%columns)*gap,y+(i/columns)*row_gap,width,pause_action(i),cursor==i,ink);''',
        'main menu button width')
    write_changed(file, source)
    file = stage / 'src/render_ui.cpp'
    source = file.read_text(encoding='utf-8')
    source = replace_once(source, 'text(x,24,"HP",cinnabar); text(x+38,24,"MP",indigo); text(x+72,24,"Tr",muted_ink);',
                          'text(x,24,"HP",cinnabar); text(x+44,24,"MP",indigo); text(x+80,24,"#",muted_ink);', 'battle HUD labels')
    source = replace_once(source, 'text(x,12,"ST",muted_ink); number(x+14,12,',
                          'text(x,12,"ST",muted_ink); number(x+17,12,', 'battle strength')
    source = replace_once(source, 'text(x+36,12,"IN",muted_ink); number(x+50,12,',
                          'text(x+42,12,"IN",muted_ink); number(x+59,12,', 'battle intelligence')
    source = replace_once(source, 'text(x+72,12,"Lv",muted_ink); number(x+86,12,',
                          'text(x+84,12,"L",muted_ink); number(x+94,12,', 'battle level')
    write_changed(file, source)
    file = stage / 'src/render_ui.cpp'
    source = '#include "english_ui.h"\n' + file.read_text(encoding='utf-8')
    source = source.replace('if(value>=100000000) { value/=100000000; suffix="M"; }',
                            'if(value>=1000000) { value/=1000000; suffix="M"; }')
    source = source.replace('else if(value>=1000000) { value/=10000; suffix="K"; }',
                            'else if(value>=10000) { value/=1000; suffix="K"; }')
    before = '''            const auto* name=reinterpret_cast<const unsigned char*>(owner_name);
            if(const auto* letter=map_flag_letter(next_codepoint(name))) {
                for(int row=0;row<7;++row) for(int col=0;col<7;++col)
                    if(letter->rows[row]&(1<<(6-col)))
                        pixel(flag_x+3+col,flag_y+2+row,letter_ink);
            }'''
    after = '''            for(int row=0;row<5;++row) {
                const unsigned bits=english_flag_row(owner_name,row);
                for(int col=0;col<7;++col) if(bits&(1u<<(6-col)))
                    pixel(flag_x+3+col,flag_y+3+row,letter_ink);
            }'''
    source = replace_once(source, before, after, 'city flag initials')
    write_changed(file, source)
    from english_layout import apply as apply_reviewed_layout
    apply_reviewed_layout(stage)
    manifest = {'language': 'en', 'source_sha256': source_hash.hexdigest(),
                'catalog_sha256': hashlib.sha256((ROOT / 'locales/en.json').read_bytes()).hexdigest(),
                'translated_literals': len(rows), 'missing': 0, 'ascii_glyphs': 95,
                'general_count': 255, 'city_count': 48, 'skill_count': 50,
                'strings': {text_id(source): {'source': source, 'en': strings[source], 'contexts': contexts}
                            for source, contexts in rows.items()}}
    write_changed(stage / 'localization_manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print('Prepared English build:', stage, 'translated literals:', len(rows))
    return stage


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    prepare()
