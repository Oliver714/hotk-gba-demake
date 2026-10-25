"""English-only layout corrections verified against the full ROM page review."""


def replace(source, before, after):
    if before not in source:
        raise ValueError('English UI layout context changed: ' + before[:100])
    return source.replace(before, after)


def apply(stage):
    path = stage / 'src/main.cpp'
    s = path.read_text(encoding='utf-8')
    changes = [
        ('render.ui_text(190,y,army.garrisoned ? "Camp" : army.retreating ? "Retreat" : "March",\n                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,36);',
         'render.ui_text(184,y,army.garrisoned ? "Camp" : army.retreating ? "Retreat" : "March",\n                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,49);'),
        ('render.ui_text(12,28,"Own",sg::muted_ink,28);',
         'render.ui_text(12,28,"Side",sg::muted_ink,28);'),
        ('render.ui_text(12,108,type==2 || type==3 ? "Use:" : "Equip:",sg::muted_ink,61);',
         'render.ui_text(12,103,type==2 || type==3 ? "Use:" : "Equip:",sg::muted_ink,61);'),
        ('render.ui_text(77,108,type==2 ? "Unlocks troop type" : type==3 ? "Unlocks formation" :\n                    bonus ? bonus : type==1 && index>=27 && index<=30 ? "Faster attacks while moving" : "Special gear effect",sg::ochre,150);',
         'render.ui_message(77,103,150,type==2 ? "Unlocks troop type" : type==3 ? "Unlocks formation" :\n                    bonus ? bonus : type==1 && index>=27 && index<=30 ? "Faster attacks while moving" : "Special gear effect",sg::ochre);'),
        ('render.ui_text(14,91,"By",sg::muted_ink);\n        render.ui_text(32,91,sg::english_compact_king(sg::king_defs[game.campaign.scenario][game.campaign.player].name),sg::cinnabar,48);\n        render.ui_text(84,91,"has united China.",sg::text_ink,140);',
         'render.ui_center(14,88,212,sg::king_defs[game.campaign.scenario][game.campaign.player].name,sg::cinnabar);\n        render.ui_center(14,103,212,"has united China.",sg::text_ink);'),
        ('const int width=columns==3 ? 72 : 104;', 'const int width=columns==3 ? 74 : 104;'),
        ('const int x=columns==3 ? 8 : 12;', 'const int x=columns==3 ? 5 : 12;'),
        ('const char* actions[]={"March","Recruit","Officers","Prisoners","Gov."};',
         'const char* actions[]={"March","Recruit","Officers","Captives","Gov."};'),
        ('render.ui_text(94,81,"Reserve",sg::muted_ink); render.ui_number(146,81,c.reserve);',
         'render.ui_text(94,81,"Res.",sg::muted_ink,34); render.ui_number(146,81,c.reserve);'),
        ('render.ui_text(14,y,sg::general_defs[g].name,game.campaign.officers[g].hp ? sg::text_ink : sg::disabled_ink,80);',
         'render.ui_text(14,y,sg::english_compact_general(g),game.campaign.officers[g].hp ? sg::text_ink : sg::disabled_ink,80);'),
        ('render.ui_text(7,133,sg::general_defs[army.generals[0]].name,sg::text_ink,60);',
         'render.ui_text(7,133,sg::english_compact_general(army.generals[0]),sg::text_ink,60);'),
        ('render.ui_number(210,y,o.soldiers);', 'render.ui_number(213,y,o.soldiers);'),
        ('render.ui_text(210,27,"#",sg::muted_ink);', 'render.ui_text(213,27,"#",sg::muted_ink);'),
        ('render.row(y,sg::general_defs[general].name,i==officer_cursor);',
         'render.row(y,sg::english_compact_general(general),i==officer_cursor);'),
        ('render.row(y,sg::king_defs[game.campaign.scenario][force].name,i==power_cursor);',
         'render.row(y,sg::english_compact_king(sg::king_defs[game.campaign.scenario][force].name),i==power_cursor);'),
        ('render.ui_text(10,27,"Ruler",sg::muted_ink);',
         'render.ui_text(10,27,"Side",sg::muted_ink,30);'),
        ('render.ui_text(43,27,sg::king_defs[game.campaign.scenario][force].name,sg::cinnabar,53);',
         'render.ui_text(45,27,sg::english_compact_king(sg::king_defs[game.campaign.scenario][force].name),sg::cinnabar,53);'),
        ('render.ui_text(8,25,"Current",sg::muted_ink);',
         'render.ui_text(8,25,"Now",sg::muted_ink,28);'),
        ('render.ui_text(72,43,"\u2192",sg::ochre,16);',
         'render.ui_text(72,43,">",sg::ochre,16);'),
        ('render.ui_text(154,5,"Prisoners",sg::muted_ink,73);',
         'render.ui_text(128,5,"Prisoners",sg::muted_ink,66);'),
        ('c.king>=0 ? "Other side" : "Neutral city",sg::muted_ink,73',
         'c.king>=0 ? "Other side" : "Unowned",sg::muted_ink,73'),
        ('blocked ? blocked : "Persuadable",', 'sg::english_prisoner_status(blocked),'),
        ('render.ui_text(10,99,"Equip",sg::muted_ink);',
         'render.ui_text(10,99,"Eq.",sg::muted_ink,28);'),
        ('render.ui_text(72,105,sg::ui_formation_name(o.formation),sg::text_ink,46);',
         'render.ui_text(72,105,sg::english_compact_formation(o.formation),sg::text_ink,46);'),
        ('render.ui_text(120,120,equipped ? equipped : "Unknown Item",sg::muted_ink,110);',
         'render.ui_text(120,120,sg::english_compact_equipped_item(o.equipped_item,equipped ? equipped : "Unknown Item"),sg::muted_ink,110);'),
        ('render.ui_text(9,105,sg::ui_force_name(o.force),sg::text_ink,59);',
         'render.ui_text(9,105,sg::english_compact_force(o.force),sg::text_ink,59);'),
        ('render.heading(12,28,sg::general_defs[general].name,168);',
         'render.heading(section==0 || section==4 ? 55 : 12,28,sg::general_defs[general].name,168);'),
        ('render.ui_text(86,48,owner,sg::text_ink,48);',
         'render.ui_text(90,48,owner,sg::text_ink,49);'),
        ('render.ui_text(86,63,show_job ?', 'render.ui_text(90,63,show_job ?'),
        ('render.heading(56,61,equipment ? equipment : "Unknown Item",164);',
         'render.ui_message(56,57,164,equipment ? equipment : "Unknown Item",sg::text_ink);'),
        ('render.line(12,83,228,83,sg::rule_ink);', 'render.line(12,89,228,89,sg::rule_ink);'),
        ('render.row(49+(i-first)*18,sg::general_defs[general].name,i==cursor);',
         'render.row(49+(i-first)*18,sg::english_compact_general(general),i==cursor);'),
        ('render.ui_text(128,50+(i-first)*18,"STR",sg::muted_ink,36);',
         'render.ui_text(128,50+(i-first)*18,"ST",sg::muted_ink,18);'),
        ('render.ui_number(166,50+(i-first)*18,game.strength(general),sg::cinnabar);',
         'render.ui_number(150,50+(i-first)*18,game.strength(general),sg::cinnabar);'),
        ('render.ui_text(187,50+(i-first)*18,"INT",sg::muted_ink,36);',
         'render.ui_text(177,50+(i-first)*18,"IN",sg::muted_ink,18);'),
        ('render.ui_number(221,50+(i-first)*18,game.intelligence(general),sg::text_ink);',
         'render.ui_number(200,50+(i-first)*18,game.intelligence(general),sg::text_ink);'),
        ('render.ui_text(98,y,sg::city_defs[officer.city].name,sg::muted_ink,58);',
         'render.ui_text(98,y,sg::english_compact_city(officer.city),sg::muted_ink,58);'),
        ('"A:Use Sel:Remove L/R:Officer B"', '"A:Use Sel:Unequip L/R:Gen B:Back"'),
        ('render.ui_number(12,y+2,i+1,sg::ochre);', 'render.ui_number(12,y,i+1,sg::ochre);'),
        ('render.ui_text(29,y+2,', 'render.ui_text(29,y,'),
        ('render.ui_text(112,52,sg::scenario_defs[slot.scenario].name,sg::text_ink,113);',
         'render.ui_text(112,52,sg::english_scenario_title(slot.scenario),sg::text_ink,113);'),
        ('render.ui_text(29,y,sg::king_defs[slot.scenario][slot.player].name,sg::text_ink,63);',
         'render.ui_text(29,y,sg::english_compact_king(sg::king_defs[slot.scenario][slot.player].name),sg::text_ink,63);'),
        ('render.number(x+53,93,', 'render.number(x+55,93,'),
        ('render.number(x+88,93,', 'render.number(x+92,93,'),
        ('render.number(x+53,106,', 'render.number(x+55,106,'),
        ('render.ui_text(x+4,93,"ST",sg::muted_ink,13);', 'render.text(x+4,93,"ST",sg::muted_ink);'),
        ('render.ui_text(x+39,93,"IN",sg::muted_ink,13);', 'render.text(x+41,93,"IN",sg::muted_ink);'),
        ('render.ui_text(x+74,93,"Lv",sg::muted_ink,13);', 'render.text(x+78,93,"Lv",sg::muted_ink);'),
        ('render.ui_text(x+4,106,"HP",sg::muted_ink,13);', 'render.text(x+4,106,"HP",sg::muted_ink);'),
        ('render.ui_text(x+39,106,"MP",sg::muted_ink,13);', 'render.text(x+41,106,"MP",sg::muted_ink);'),
        ('render.ui_text(x+74,106,"#",sg::muted_ink,13);', 'render.text(x+78,106,"#",sg::muted_ink);'),
        ('render.ui_text(x+47,119,sg::english_compact_formation(officer.formation),sg::muted_ink,43);',
         'render.ui_text(x+54,119,sg::english_card_formation(officer.formation),sg::muted_ink,30);'),
        ('render.ui_text(127,108,"Path",sg::muted_ink,30);',
         'render.ui_text(127,101,"To",sg::muted_ink,30);'),
        ('render.ui_text(158,108,sg::city_defs[sg::army_destination(game.campaign,army_index)].name,sg::text_ink,72);',
         'render.ui_text(158,101,sg::english_compact_city(sg::army_destination(game.campaign,army_index)),sg::text_ink,72);'),
        ('render.ui_text(127,91,"Gen.",sg::muted_ink,30); render.ui_number(162,91,army.count);',
         'render.ui_text(127,85,"Gen.",sg::muted_ink,30); render.ui_number(162,85,army.count);'),
        ('render.ui_text(127,124,game.notice[0] ? game.notice : army.garrisoned ? "Stationed" : army.retreating ? "Retreat" : "Marching",\n                           game.notice[0] ? sg::warning_ink : army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,104);',
         'render.ui_text(127,117,army.garrisoned ? "Stationed" : army.retreating ? "Retreat" : "Marching",\n                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,104);'),
        ('render.footer("A:OK Sel:Locate B:Map");',
         'render.footer("A:OK Sel:Locate B:Map",game.notice[0] ? game.notice : nullptr);'),
    ]
    for before, after in changes:
        s = replace(s, before, after)
    path.write_text(s, encoding='utf-8')

    path = stage / 'include/english_ui.h'
    s = path.read_text(encoding='utf-8')
    end = s.rfind('}')
    s = s[:end] + '''inline const char* english_card_formation(int formation) {
    static const char* const names[]={"Sqr","Cir","Wdg","Gse","Mys","Fsh","Hok"};
    return names[formation<0?0:formation>6?6:formation];
}
inline const char* english_prisoner_status(const char* blocked) {
    return !blocked ? "Ready" : english_same(blocked,"Loyal to their ruler") ? "Loyal" :
        english_same(blocked,"In transit") ? "Transit" : "Blocked";
}
inline const char* english_scenario_title(int scenario) {
    static const char* const names[]={"Yellow Turbans","Dong Zhuo","Rival Warlords","Red Cliffs","Three Kingdoms"};
    return names[scenario<0?0:scenario>4?4:scenario];
}
''' + s[end:]
    path.write_text(s, encoding='utf-8')

    path = stage / 'src/render_ui.cpp'
    s = path.read_text(encoding='utf-8')
    a = s.index('void Render::ui_message(')
    b = s.index('// Raised/recessed', a)
    s = s[:a] + '''void Render::ui_message(int x,int y,int width,const char* string,int color) {
    if(!string || !*string) return;
    const char* p=string;
    for(int row=0;row<2 && *p;++row) {
        while(*p==' ')++p;
        if(!*p)return;
        if(row==1) { ui_text(x,y,p,color,width); return; }
        char line[192];int count=0,measured=0,last_space=-1;
        while(p[count] && count<190) {
            const int step=advance(uint8_t(p[count]),false);
            if(measured+step-1>width)break;
            measured+=step;line[count]=p[count];
            if(p[count]==' ')last_space=count;
            ++count;
        }
        if(p[count] && p[count]!=' ' && last_space>0)count=last_space;
        if(!count){line[0]=*p;count=1;}
        int used=count;while(used>0 && line[used-1]==' ')--used;
        line[used]=0;ui_text(x,y,line,color,width);p+=count;y+=14;
    }
}
''' + s[b:]
    s = replace(s, 'text(x,0,general_defs[fighter.general].name,text_ink);',
                   'text(x,0,english_compact_general(fighter.general),text_ink);')
    path.write_text(s, encoding='utf-8')

    path = stage / 'src/render.bn_iwram.cpp'
    s = path.read_text(encoding='utf-8')
    for before, after in [
        ('ui_text(84,3,army_index>=0 ? "Redirect" : "From",muted_ink,65);\n    if(army_index<0) ui_text(98,3,city_defs[origin].name,indigo,52);',
         'ui_text(84,3,army_index>=0 ? "Redirect" : english_compact_city(origin),army_index>=0 ? muted_ink : indigo,65);'),
        ('ui_text(4,119,"Target",muted_ink);', 'ui_text(4,119,"To",muted_ink,24);'),
        ('"Dir:City A:March B:Zoom Start:Back"', '"Dir:City A:Go B:Zoom Start:Back"'),
        ('ui_text(label.x,label.y,general_defs[a.generals[0]].name,text_ink,60);',
         'ui_text(label.x,label.y,english_compact_general(a.generals[0]),text_ink,60);'),
    ]:
        s = replace(s, before, after)
    path.write_text(s, encoding='utf-8')
