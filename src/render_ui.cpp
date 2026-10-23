#include "render.h"
#include "map_flag_letters.h"
#include "map_markers.h"

namespace sg {
const char* ui_force_name(int force) {
    static const char* labels[]={"朴刀兵","长枪兵","弓箭兵","大刀兵","飞刀兵","链锤兵","黄巾兵","蛮族兵","藤甲兵","武斗兵","铁锤兵"};
    return labels[clamp(force,0,unit_types-1)];
}
const char* ui_formation_name(int formation) {
    static const char* labels[]={"方阵","圆阵","锥阵","雁阵","玄襄阵","鱼鳞阵","钩形阵"};
    return labels[clamp(formation,0,6)];
}
namespace {
uint32_t next_codepoint(const unsigned char*& p) {
    uint32_t cp=*p++;
    if(cp>=0xe0 && cp<0xf0 && p[0] && p[1]) { cp=((cp&15)<<12)|((p[0]&63)<<6)|(p[1]&63); p+=2; }
    else if(cp>=0xc0 && cp<0xe0 && p[0]) { cp=((cp&31)<<6)|(p[0]&63); ++p; }
    return cp;
}
const UiGlyph* find_glyph(uint32_t cp,bool large) {
    const auto* table=large ? heading_glyphs : ui_glyphs;
    int low=0, high=(large ? heading_glyph_count : ui_glyph_count)-1;
    while(low<=high) { const int mid=(low+high)/2;
        if(table[mid].codepoint==cp) return table+mid;
        if(table[mid].codepoint<cp) low=mid+1; else high=mid-1;
    }
    return nullptr;
}
int role(int color) {
    static const int map[16]={paper,text_ink,ochre,cinnabar,indigo,jade,surface,rule_ink,
        pale_paper,muted_ink,shadow_ink,text_ink,indigo,seal_ink,warning_ink,muted_ink};
    return color<16 ? map[color] : color;
}
void paint_glyph(Render& render,int x,int y,uint32_t cp,int color,bool large) {
    const auto* g=find_glyph(cp,large);
    if(!g) { render.line(x+2,y+2,x+8,y+2,color); render.line(x+2,y+2,x+2,y+9,color);
        render.line(x+8,y+2,x+8,y+9,color); render.line(x+2,y+9,x+8,y+9,color); return; }
    render.ui_glyph(x,y,*g,large ? 16 : 12,color);
}
int advance(uint32_t cp,bool large) {
    const auto* g=find_glyph(cp,large);
    return (g ? g->width : (large ? 16 : 12))+1;
}
}

int Render::text_width(const char* string,bool large) const {
    if(!string) return 0;
    const auto* p=reinterpret_cast<const unsigned char*>(string);
    int width=0;
    while(*p) width+=advance(next_codepoint(p),large);
    return maximum(0,width-1);
}
void Render::ui_text(int x,int y,const char* string,int color,int width) {
    if(!string || !*string) return;
    width=minimum(width,240-x); color=role(color);
    const bool truncate=text_width(string)>width;
    const int end=x+width-(truncate ? 18 : 0);
    const auto* p=reinterpret_cast<const unsigned char*>(string);
    while(*p) { const uint32_t cp=next_codepoint(p); const int step=advance(cp,false);
        if(x+step-1>end) break;
        paint_glyph(*this,x,y,cp,color,false); x+=step;
    }
    if(truncate && width>=18) for(int dot=0;dot<3;++dot) { rect(x+dot*5,y+9,2,2,color); }
}
void Render::heading(int x,int y,const char* string,int width) {
    const auto* p=reinterpret_cast<const unsigned char*>(string);
    const int end=minimum(240,x+width);
    while(*p) { const uint32_t cp=next_codepoint(p); const int step=advance(cp,true);
        if(x+step-1>end) break;
        paint_glyph(*this,x,y,cp,text_ink,true); x+=step;
    }
}
void Render::ui_number(int x,int y,int value,int color) {
    char buffer[12]; int end=11; buffer[end]=0;
    const bool negative=value<0; if(negative) value=-value;
    do { buffer[--end]=char('0'+value%10); value/=10; } while(value && end>1);
    if(negative) buffer[--end]='-';
    ui_text(x,y,buffer+end,color);
}
void Render::ui_amount(int x,int y,uint32_t value) {
    const char* suffix="";
    if(value>=100000000) { value/=100000000; suffix="亿"; }
    else if(value>=1000000) { value/=10000; suffix="万"; }
    char buffer[12]; int end=11; buffer[end]=0;
    do { buffer[--end]=char('0'+value%10); value/=10; } while(value && end>0);
    ui_text(x,y,buffer+end);
    ui_text(x+text_width(buffer+end)+1,y,suffix);
}
void Render::ui_unsigned(int x,int y,uint32_t value,int color) {
    char buffer[11];int end=10;buffer[end]=0;
    do { buffer[--end]=char('0'+value%10);value/=10; } while(value);
    ui_text(x,y,buffer+end,color);
}
void Render::ui_center(int x,int y,int width,const char* string,int color) {
    ui_text(x+maximum(0,(width-text_width(string))/2),y,string,color,width);
}
void Render::ui_message(int x,int y,int width,const char* string,int color) {
    if(!string || !*string) return;
    char line[192]; int used=0, measured=0, row=0;
    const auto* p=reinterpret_cast<const unsigned char*>(string);
    while(*p) {
        const auto* start=p; const uint32_t cp=next_codepoint(p); const int step=advance(cp,false);
        if(measured+step>width && used) {
            line[used]=0; ui_text(x,y,line,color,width); y+=14; ++row; used=0; measured=0;
            if(row==1) { ui_text(x,y,reinterpret_cast<const char*>(start),color,width); return; }
        }
        while(start<p && used<190) line[used++]=char(*start++);
        measured+=step;
    }
    line[used]=0; ui_text(x,y,line,color,width);
}
// Raised/recessed window used by every menu panel, row and button. All
// decoration is drawn strictly inside the caller's rectangle, so existing
// layout metrics are unchanged and neighbouring boxes cannot be overpainted.
// GBA authentic style: flat fills, high contrast borders, no gradients.
void Render::ui_window(int x,int y,int w,int h,bool selected) {
    if(w<6 || h<6) { rect(x,y,w,h,selected ? focus_paper : paper); return; }
    // Outer border
    rect(x,y,w,h,selected ? cinnabar : rule_ink);
    // Inner fill - flat color, no gradient for authentic GBA look
    rect(x+1,y+1,w-2,h-2, selected ? focus_paper : surface);
    // High-contrast beveled edges for 3D effect
    rect(x+1,y+1,w-2,1,edge_light);
    rect(x+1,y+1,1,h-2,edge_light);
    rect(x+1,y+h-2,w-2,1,shadow_soft);
    rect(x+w-2,y+1,1,h-2,shadow_soft);
    rect(x+2,y+h-3,w-4,1,paper);
    rect(x+w-3,y+2,1,h-4,paper);
    if(selected) {
        rect(x+1,y+1,3,h-2,cinnabar);
        rect(x+4,y+2,1,h-4,gold_hi);
    }
}
void Render::ui_box(int x,int y,int w,int h,bool selected) { ui_window(x,y,w,h,selected); }
void Render::footer(const char* first,const char* second) {
    const int y=second ? 132 : 144;
    // Keep texture away from the small body text.
    rect(0,y,240,160-y,surface);
    rect(0,y,240,1,rule_ink); rect(0,y+1,240,1,focus_paper);
    ui_text(7,y+3,first,text_ink,226);
    if(second) ui_text(7,y+16,second,muted_ink,226);
}
void Render::page_marker(int current,int total,int y) {
    if(total<=0) return;
    char buffer[24];int used=0;
    auto append=[&](unsigned value) {
        char digits[10];int count=0;
        do { digits[count++]=char('0'+value%10);value/=10; } while(value);
        while(count) buffer[used++]=digits[--count];
    };
    append(unsigned(clamp(current,0,total-1))+1);buffer[used++]='/';
    append(unsigned(total));buffer[used]=0;
    ui_text(234-text_width(buffer),y,buffer,muted_ink);
}
void Render::panel(const char* title) {
    faction_palette(false);
    // Bright background with subtle texture for GBA authenticity
    clear(pale_paper);

    // Add simple decorative pattern for visual interest
    // Subtle horizontal bands every 32 pixels
    for(int y = 24; y < 160; y += 32) {
        rect(0, y, 5, 1, paper_mid);
        rect(235, y, 5, 1, paper_mid);
    }

    // Corner decorative elements
    // Top-left corner
    rect(0, 24, 2, 16, paper_mid);
    rect(0, 24, 8, 2, paper_mid);

    // Top-right corner
    rect(238, 24, 2, 16, paper_mid);
    rect(232, 24, 8, 2, paper_mid);

    // Title band: flat color, minimal gradient for GBA authenticity
    rect(0,0,240,23, paper);
    rect(0,22,240,1,rule_ink);
    rect(0,23,240,1,focus_paper);
    rect(6,4,6,15,edge_dark);
    rect(7,5,4,13,cinnabar);
    rect(8,6,2,11,gold_hi);
    heading(18,3,title,217);
}
// Draw infrequent header content from ROM, preserving the combat IWRAM budget.
__attribute__((noinline))
void Render::battle_general_headers(const Game& game,int controlled) {
    for(int side=0;side<2;++side) {
        const auto& fighter=game.battle.sides[side];
        const int x=side==controlled ? 8 : 130;
        text(x,0,general_defs[fighter.general].name,text_ink);
        text(x+86,0,side==controlled ? "我" : "敌",side==controlled ? indigo : warning_ink);
        text(x,12,"武",muted_ink); number(x+14,12,game.strength(fighter.general),cinnabar);
        text(x+36,12,"智",muted_ink); number(x+50,12,game.intelligence(fighter.general),text_ink);
        text(x+72,12,"级",muted_ink); number(x+86,12,game.level(fighter.general),text_ink);
        text(x,24,"体",cinnabar); text(x+38,24,"技",indigo); text(x+72,24,"兵",muted_ink);
    }
}
void Render::title_main(int selected,const char* message,uint32_t animation_frame) {
    faction_palette(false);
    clear(paper); ui_bitmap(0,0,240,160,silk_title_pixels);
    title_leaves(animation_frame,animation_frame);
    // The logo and tagline sit on the bright illustration, so they use the near
    // black ink rather than the cream body text, which would vanish there.
    ui_bitmap(48,32,144,32,silk_logo_pixels);
    ui_center(0,66,240,message && *message ? message : "群雄逐鹿 · 战略篇",message && *message ? warning_ink : edge_dark);
    ui_box(44,84,152,44);
    compact_button(51,87,138,"开始游戏",selected==0);
    compact_button(51,108,138,"读取存档",selected==1);
    footer("A确认   SELECT读取");
}
void Render::title_leaves(uint32_t animation_frame,uint32_t previous_frame) {
    // Sparse paper-gold leaves, driven by the UI clock, never campaign RNG.
    // Tiny pixel silhouettes and integer swaying keep this cheaper than art
    // animation frames. The title and button silhouettes are protected.
    // Restore all old silhouettes before painting any new ones, including
    // overlapping leaves. No full-screen clear, bitmap, font, or window work.
    for(int pass=0;pass<2;++pass) {
    if(pass==0 && animation_frame==previous_frame) continue;
    const uint32_t time=(pass==0 ? previous_frame : animation_frame)%63360;
    for(int i=0;i<8;++i) {
        const int phase=int(time/9)+i*11;
        const int wave=phase%24;
        const int sway=(wave<12 ? wave : 24-wave)-6;
        const int x=(i*37+17+int(time/(13+i))+sway+256)%256-8;
        const int y=(int(time/(4+i%3))+i*23)%176-8;
        if(y<0 || y>138 || (x>36 && x<198 && y>25 && y<129)) continue;
        if(pass==0) {
            const int left=maximum(0,x),right=minimum(240,x+7);
            if(right>left) for(int row=0;row<5;++row)
                ui_bitmap(left,y+row,right-left,1,silk_title_pixels+(y+row)*240+left);
            continue;
        }
        const bool edge=(phase/6)%3==0;
        if(edge) {
            line(x,y,x+4,y+3,paper_hi);
            pixel(x+2,y+1,edge_light);
            pixel(x+5,y+4,paper_mid);
        } else {
            rect(x+2,y,3,1,edge_light);
            rect(x+1,y+1,5,2,paper_hi);
            rect(x,y+2,4,2,edge_light);
            rect(x+1,y+4,2,1,paper_hi);
            line(x+1,y+3,x+5,y+1,paper_mid);
            pixel(x+6,y,paper_mid);
        }
    }
    }
}
void Render::title(const char* scenario,int selected,const char* message) {
    panel("选择剧本"); ui_text(9,28,scenario,muted_ink,220);
    for(int i=0;i<5;++i) row(49+i*18,scenario_defs[i].name,i==selected);
    footer(message && *message ? message : "上下选择   A开始   B返回");
}
void Render::row(int y,const char* label,bool selected,int color) {
    ui_window(6,y-2,228,17,selected);
    if(selected) ui_cursor(13,y+1,cinnabar);
    ui_text(24,y,label,selected ? text_ink : role(color),202);
}
void Render::button(int x,int y,int w,const char* label,bool selected,int color) {
    ui_box(x,y,w,19,selected);
    ui_text(x+9,y+3,label,color==disabled_ink ? disabled_ink : selected ? text_ink : role(color),w-15);
}
void Render::compact_button(int x,int y,int w,const char* label,bool selected) {
    ui_box(x,y,w,18,selected); ui_center(x+5,y+3,w-10,label,text_ink);
}
void Render::portrait_large(int x,int y,int general) {
    // Framed like a hanging scroll portrait: dark keyline, lit upper left.
    rect(x-2,y-2,36,36,edge_dark);
    rect(x-1,y-1,32,1,edge_light);
    rect(x-1,y-1,1,32,edge_light);
    rect(x-1,y+32,32,1,shadow_soft);
    rect(x+32,y-1,1,32,shadow_soft);
    if(unsigned(general)>=general_count) return;
    for(int py=0;py<32;++py) for(int px=0;px<32;++px) {
        const int sx=px*24/32, sy=py*24/32;
        const int value=face_pixels[general][(sy*24+sx)>>1];
        pixel(x+px,y+py,(value>>((sx&1)*4))&15);
    }
}
void Render::city_scene(int x,int y,int owner) {
    // Quiet architectural vignette, built at the final display resolution.
    rect(x,y,64,27,pale_paper);
    line(x+4,y+24,x+61,y+24,rule_ink);
    rect(x+8,y+14,49,10,shadow_ink); rect(x+9,y+15,47,8,muted_ink);
    for(int col=9;col<57;col+=8) rect(x+col,y+11,5,5,muted_ink);
    rect(x+26,y+7,17,13,muted_ink);
    line(x+22,y+8,x+34,y+2,text_ink); line(x+34,y+2,x+47,y+8,text_ink);
    rect(x+31,y+16,7,8,text_ink); line(x+34,y+2,x+34,y-2,text_ink);
    rect(x+35,y-2,8,4,owner ? cinnabar : indigo);
}
__attribute__((noinline))
void Render::map_city(int x,int y,int color,bool zoomed,bool occupied,
                      const char* owner_name,int letter_ink) {
    if(!zoomed) {
        // At overview scale, ownership is the entire mark: 25 solid pixels.
        rect(x-3,y-3,7,7,edge_dark);
        rect(x-2,y-2,5,5,color);
        return;
    }
    // Neutral, compact 11px gate; the large flat flag carries ownership.
    rect(x-5,y-2,11,7,edge_dark);
    rect(x-4,y-1,9,5,paper_mid);
    rect(x-4,y-3,3,2,edge_dark); rect(x+2,y-3,3,2,edge_dark);
    pixel(x-3,y-2,paper_mid); pixel(x+3,y-2,paper_mid);
    rect(x-1,y+1,3,4,edge_dark);
    pixel(x-3,y+2,paper_deep); pixel(x+3,y+2,paper_deep);
    if(occupied) {
        // Keep flags readable at the top/right edges of the clipped map.
        const auto flag=map_flag_origin(x,y,map_field_top);
        const int flag_x=flag.x,flag_y=flag.y;
        line(flag_x-1,minimum(y-1,flag_y),flag_x-1,maximum(y+1,flag_y+map_flag_height-1),edge_dark);
        rect(flag_x,flag_y,map_flag_width,map_flag_height,edge_dark);
        rect(flag_x+1,flag_y+1,map_flag_width-2,map_flag_height-2,color);
        if(owner_name && *owner_name) {
            const auto* name=reinterpret_cast<const unsigned char*>(owner_name);
            if(const auto* letter=map_flag_letter(next_codepoint(name))) {
                for(int row=0;row<7;++row) for(int col=0;col<7;++col)
                    if(letter->rows[row]&(1<<(6-col)))
                        pixel(flag_x+3+col,flag_y+2+row,letter_ink);
            }
        }
    }
}
void Render::city_frame(int x,int y) { ui_box(x,y,70,118); }
void Render::city_frame_b(int x,int y) { ui_box(x,y,76,118); }
void Render::city_command_frame(int x,int y) { ui_box(x,y,82,118); }
void Render::formation_frame(int x,int y) { ui_box(x,y,86,113); }
void Render::troop_type_frame(int x,int y) { ui_box(x,y,70,24); }
void Render::officer_skill_frame(int x,int y) { ui_box(x,y,70,24); }
void Render::major_frame(int,int) { panel("任命太守"); }
void Render::commander_frame(int,int) { panel("前军统领"); }
void Render::assemble_frame(int,int) { panel("出征编组"); }
void Render::officer_list_frame(int,int) { panel("武将名册"); }
void Render::city_button(int x,int y,int item,bool selected,bool enabled) {
    static const char* labels[]={"搜索","筑城防御","开发人口","武将资料","武将晋升","出征部队"};
    if(enabled) ui_window(x,y,72,17,selected);
    else {
        // Recessed, drained window: the command exists but no officer qualifies.
        rect(x,y,72,17,shadow_soft);
        ui_gradient(x+1,y+1,70,15,3,3,1);
        rect(x+1,y+1,70,1,shadow_soft);
        rect(x+1,y+1,1,15,shadow_soft);
        if(selected) rect(x,y,3,17,disabled_ink);
    }
    ui_text(x+9,y+2,labels[clamp(item,0,5)],enabled ? text_ink : disabled_ink,57);
}
}
