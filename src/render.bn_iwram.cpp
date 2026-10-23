#include "render.h"
#include "map_markers.h"
#include "city_navigation.h"
#include "battle_projection.h"
#include "battle_visual_rules.h"
#include "battle_camera.h"
#include "projectile_arrow_art.h"
#include "zoom_sprites.h"
#include "bn_bg_palette_item.h"
#include "bn_regular_bg_item.h"
#include "bn_regular_bg_tiles_item.h"
#include "bn_regular_bg_map_ptr.h"
#include "bn_span.h"
#include "bn_color.h"
#include "original_rules.h"
#include "battle_sprites.h"
#include "battle_effects.h"
#include "additive_palette.h"
#include "effect_blend.h"
#include "effect_spans.h"
#include "magic_dragon.h"
#include "magic_healing.h"
#include "magic_reinforce_fail.h"
#include "magic_waves.h"
#include "magic_m011_waves.h"
#include "magic_m011_rows.h"
#include "magic_m012_projectile.h"
#include "magic_m012_burst.h"
#include "magic_m013_cyclone.h"
#include "magic_sword_sweep.h"
#include "magic_m014_sparks.h"
#include "magic_clone_slash.h"
#include "magic_eight_gates.h"
#include "magic_m020_field.h"
#include "magic_m007_catapult.h"
#include "magic_m007_break.h"
#include "magic_m005_mines.h"
#include "magic_m008_stonefire.h"
#include "magic_lightning.h"
#include "magic_strikes.h"
#include "magic_fire_hit.h"
#include "magic_yellow_dragon.h"
#include "magic_chariot.h"
#include "magic_chariot_hit.h"
#include "magic_arrows.h"
#include "magic_m001_mana.h"
#include "bn_core.h"
#include "bn_memory.h"
#include "bn_bg_palette_ptr.h"
#include "../vendor/butano/butano/hw/include/bn_hw_dma.h"

extern "C" volatile uint32_t sg_probe[];

namespace {
BN_DATA_EWRAM bn::tile screen_tiles[1202];
BN_DATA_EWRAM bn::tile battle_backdrop[1202];
// White-light palette matching is compiled to ROM by import_additive_palette.
// Static strategic field, including city ownership. Separate from the combat
// backdrop: opening a map must not destroy a suspended battle's cached HUD.
constexpr int map_cache_first_row=sg::map_field_top/8;
constexpr int map_cache_tile_rows=(sg::map_field_top+sg::map_field_height+7)/8-map_cache_first_row;
BN_DATA_EWRAM bn::tile map_backdrop[map_cache_tile_rows*60];
BN_DATA_EWRAM int cached_map_center=-2;
bool cached_map_zoom=false;
BN_DATA_EWRAM int8_t cached_map_owners[sg::city_count];
bool map_ui_active=false;
BN_DATA_EWRAM bn::regular_bg_map_cell screen_map[1024];
int clip_top=0, clip_bottom=160;
sg::BattleProjection projection;
bool project_field=false;
// Zoomed blitters live in ROM; normal-size combat retains its fast RAM path.
__attribute__((section(".text")))
void scaled_sprite(int x,int y,int w,int h,const uint8_t* source,bool flip,int mix) {
    const int shift=projection.shift;
    const int left=sg::maximum(0,projection.x(x));
    const int right=sg::minimum(240,projection.x(x+w-1)+1);
    const int top=sg::maximum(sg::battle_view_top,projection.y(y));
    const int bottom=sg::minimum(sg::battle_view_bottom,projection.y(y+h-1)+1);
    auto* bytes=reinterpret_cast<uint8_t*>(screen_tiles);
    if(mix>=0) {
        const int factor=1<<shift;
        const auto* table=&sg::additive_palette[mix][0][0];
        for(int yy=top;yy<bottom;++yy) {
            const int sy=sg::clamp((yy-sg::battle_view_center_y)*factor+sg::battle_view_center_y-y,0,h-1);
            const auto* row=source+sy*w;
            const int row_offset=(yy>>3)*30*64+(yy&7)*8;
            int xx=left;
            int sx=(left-120)*factor+120-x;
            // Projection floors the first output coordinate. Only this first
            // sample can fall before the source edge; preserve its clamp.
            if(xx<right && sx<0) {
                sg::blend_effect_strided_span(bytes+row_offset+(xx>>3)*64+(xx&7),
                    row+(flip ? w-1 : 0),1,table,1);
                ++xx;sx+=factor;
            }
            while(xx<right) {
                const int count=sg::minimum(right-xx,8-(xx&7));
                sg::blend_effect_strided_span(bytes+row_offset+(xx>>3)*64+(xx&7),
                    row+(flip ? w-1-sx : sx),count,table,flip ? -factor : factor);
                xx+=count;sx+=count*factor;
            }
        }
        return;
    }
    for(int yy=top;yy<bottom;++yy) {
        const int sy=sg::clamp((yy-sg::battle_view_center_y)*(1<<shift)+sg::battle_view_center_y-y,0,h-1);
        for(int xx=left;xx<right;++xx) {
            int sx=sg::clamp((xx-120)*(1<<shift)+120-x,0,w-1);
            if(flip) sx=w-1-sx;
            const int color=source[sy*w+sx];
            if(!color) continue;
            auto& out=bytes[((yy>>3)*30+(xx>>3))*64+(yy&7)*8+(xx&7)];
            out=mix>=0 && color>=118 && color<166 ? sg::additive_palette[mix][color-118][out] : color;
        }
    }
}
__attribute__((section(".text")))
void scaled_packed(int x,int y,const sg::PackedSprite& sprite) {
    x &= ~3;
    const int top=sg::maximum(sg::battle_view_top,projection.y(y));
    const int bottom=sg::minimum(sg::battle_view_bottom,projection.y(y+sprite.height-1)+1);
    auto* bytes=reinterpret_cast<uint8_t*>(screen_tiles);
    const int factor=1<<projection.shift;
    for(int yy=top;yy<bottom;++yy) {
        const int sy=sg::clamp((yy-sg::battle_view_center_y)*factor+sg::battle_view_center_y-y,0,sprite.height-1);
        for(int i=sprite.rows[sy];i<sprite.rows[sy+1];++i) {
            const auto& span=sprite.spans[i];
            for(int k=0;k<4;++k) {
                const int vx=x+int(span.column)+k;
                if((vx-120)&(factor-1)) continue;
                const int xx=projection.x(vx);
                const int color=(span.pixels>>(k*8))&255;
                if(unsigned(xx)<240 && color)
                    bytes[((yy>>3)*30+(xx>>3))*64+(yy&7)*8+(xx&7)]=uint8_t(color);
            }
        }
    }
}
bool combat_active=false;
bool battle_mirrored=false;
bn::tile* framebuffer_vram=nullptr;
bool upload_pending=false, upload_full=true, upload_hud=false;
void commit_framebuffer() {
    if(!upload_pending) return;
    if(upload_full) {
        bn::hw::dma::copy_words(screen_tiles,1202*8,framebuffer_vram);
    } else {
        if(upload_hud) bn::hw::dma::copy_words(screen_tiles,4*60*8,framebuffer_vram);
        // Tiles intersecting the field and the changing skill status line.
        bn::hw::dma::copy_words(screen_tiles+4*60,16*60*8,framebuffer_vram+4*60);
    }
    upload_pending=false; upload_full=false; upload_hud=false;
}
const bn::color colors[256] = {
    bn::color(3,5,8), bn::color(29,29,26), bn::color(31,22,8), bn::color(25,7,8),
    bn::color(8,17,29), bn::color(8,22,17), bn::color(1,3,7), bn::color(13,15,19),
    bn::color(4,10,15), bn::color(24,20,13), bn::color(13,8,5), bn::color(27,28,24),
    bn::color(5,20,23), bn::color(18,11,23), bn::color(30,14,4), bn::color(14,17,21),
#include "battle_palette.inc"
    bn::color(12,15,8), bn::color(9,12,6), bn::color(17,19,11),
    bn::color(21,18,12), bn::color(16,13,8), bn::color(25,22,16),
#include "effect_palette.inc"
#include "ui_palette.inc"
#include "ui_extra_palette.inc"
};
// Stable, distinct banner pigments for the 26 ruler IDs. These use title artwork
// palette slots only while a map is visible; title_main restores the originals.
const bn::color faction_colors[sg::max_kings] = {
    bn::color(27,6,5),   bn::color(4,10,27),  bn::color(3,22,9),   bn::color(30,16,3),
    bn::color(17,6,24),  bn::color(2,21,23),  bn::color(29,24,3),  bn::color(19,3,7),
    bn::color(4,18,29),  bn::color(16,20,4),  bn::color(26,9,17),  bn::color(3,22,15),
    bn::color(25,10,3),  bn::color(9,7,21),   bn::color(18,25,7),  bn::color(22,12,8),
    bn::color(10,24,29), bn::color(22,9,25),  bn::color(12,17,24), bn::color(29,20,11),
    bn::color(12,4,17),  bn::color(3,14,7),   bn::color(18,12,3),  bn::color(3,8,16),
    bn::color(26,14,20), bn::color(9,22,18)
};
bool faction_palette_enabled = false;
uint32_t& tile_word(int tile, int row, int half) {
    return screen_tiles[tile*2+(row>>2)].data[(row&3)*2+half];
}
void legacy_row(int tile, int row, uint32_t packed,int palette_offset=0) {
    for(int half=0;half<2;++half) {
        uint32_t expanded=0;
        for(int px=0;px<4;++px) expanded |= (palette_offset+((packed >> ((half*4+px)*4)) & 15)) << (px*8);
        tile_word(tile,row,half)=expanded;
    }
}
struct MapPalettePairs {
    uint16_t values[256]={};
    constexpr MapPalettePairs() {
        for(int value=0;value<256;++value)
            values[value]=uint16_t((sg::paper_map_palette+(value&15)) |
                ((sg::paper_map_palette+(value>>4))<<8));
    }
};
constexpr MapPalettePairs map_palette_pairs;
BN_CODE_IWRAM __attribute__((noinline)) void map_row(int tile,int row,uint32_t packed) {
    tile_word(tile,row,0)=uint32_t(map_palette_pairs.values[packed&255]) |
        (uint32_t(map_palette_pairs.values[(packed>>8)&255])<<16);
    tile_word(tile,row,1)=uint32_t(map_palette_pairs.values[(packed>>16)&255]) |
        (uint32_t(map_palette_pairs.values[packed>>24])<<16);
}
int map_x(int x) { return x * 225 / 1200 + 6; }
int map_y(int y) { return y * 96 / 950 + 19; }
constexpr int map_zoom_factor = 2;
int map_view_x(int world_x, int selected, bool zoomed,const sg::MapPoint* center=nullptr) {
    if(!zoomed) return map_x(world_x);
    return 120 + (world_x-(center ? center->x : int(sg::city_defs[selected].x)))*225*map_zoom_factor/1200;
}
int map_view_y(int world_y, int selected, bool zoomed, int height,const sg::MapPoint* center=nullptr) {
    if(!zoomed) return sg::map_field_top+world_y*height/950;
    return sg::map_field_top+height/2 + (world_y-(center ? center->y : int(sg::city_defs[selected].y)))*96*map_zoom_factor/950;
}
int king_ink(int king, int) { return king < 0 ? sg::disabled_ink : sg::faction_palette_first + king; }
int map_ground_ink(int world_x, int world_y) {
    int counts[16] = {};
    for(int oy = -18; oy <= 18; oy += 9) for(int ox = -18; ox <= 18; ox += 9) {
        const int x = sg::clamp((world_x + ox) * 225 / 1200, 0, 224) + 6;
        const int y = sg::clamp((world_y + oy) * 96 / 950, 0, 95);
        const uint32_t packed = sg::terrain_overview_rows[y * 30 + (x >> 3)];
        const int color = int((packed >> ((x & 7) * 4)) & 15);
        if(color == sg::green || color == sg::dark_green || color == sg::tan ||
           color == sg::brown || color == sg::orange) {
            ++counts[color];
        }
    }
    const int candidates[] = {sg::tan, sg::green, sg::dark_green, sg::brown, sg::orange};
    int best = sg::tan;
    for(int color : candidates) if(counts[color] > counts[best]) best = color;
    return best == sg::brown || best == sg::orange ? sg::tan : best;
}
const sg::Glyph* glyph(uint32_t cp) {
    int low = 0, high = sg::glyph_count - 1;
    while(low <= high) {
        int mid = (low + high) / 2;
        if(sg::glyphs[mid].codepoint == cp) return &sg::glyphs[mid];
        if(sg::glyphs[mid].codepoint < cp) low = mid + 1; else high = mid - 1;
    }
    return nullptr;
}
}

namespace sg {
Render::Render() :
    _tiles(bn::regular_bg_tiles_ptr::allocate(1202,bn::bpp_mode::BPP_8,false)),
    _bg(bn::regular_bg_ptr::create(8,48,bn::regular_bg_map_ptr::create(
        bn::regular_bg_map_item(screen_map[0],bn::size(32,32)),_tiles,
        bn::bg_palette_ptr::create(bn::bg_palette_item(bn::span<const bn::color>(colors,256),bn::bpp_mode::BPP_8))))) {
    framebuffer_vram=_tiles.vram()->data();
    bn::core::set_vblank_callback(commit_framebuffer);
    for(int y = 0; y < 32; ++y) for(int x = 0; x < 32; ++x) screen_map[y * 32 + x] = uint16_t(x < 30 && y < 20 ? y * 30 + x : 600);
    auto map = _bg.map();
    map.reload_cells_ref();
    clear();
}

void Render::faction_palette(bool enabled) {
    if(faction_palette_enabled == enabled) return;
    auto palette = _bg.palette();
    for(int i=0;i<faction_palette_count;++i) {
        palette.set_color(faction_palette_first+i,enabled ? faction_colors[i] : colors[faction_palette_first+i]);
    }
    faction_palette_enabled=enabled;
}

void Render::battle_orientation(bool mirrored) {
    if(battle_mirrored == mirrored) return;
    static_assert(battle_view_top % 8 == 0 && battle_view_bottom % 8 == 0);
    for(int y=battle_view_top/8;y<battle_view_bottom/8;++y)
        for(int x=0;x<30;++x)
            screen_map[y*32+x]=uint16_t(y*30+(mirrored ? 29-x : x)+(mirrored ? 0x400 : 0));
    auto map=_bg.map();
    map.reload_cells_ref();
    battle_mirrored=mirrored;
}

void Render::clear(int color) {
    map_ui_active=false;
    battle_orientation(false);
    combat_active=false;
    upload_full=true;
    uint32_t value = unsigned(color) * 0x01010101;
    for(int i = 0; i < 1202; ++i) for(auto& row : screen_tiles[i].data) row = value;
    if(color>=paper) { rect(0,0,240,1,rule_ink); rect(0,159,240,1,rule_ink); return; }
    rect(0,0,240,2,road); rect(0,158,240,2,road);
    rect(0,0,2,160,dark_green); rect(238,0,2,160,dark_green);
    for(int x = 7; x < 240; x += 16) {
        pixel(x,3,dark_green); pixel(x+4,3,road);
        pixel(x+8,156,road); pixel(x+12,156,dark_green);
    }
}

void Render::pixel(int x, int y, int color) {
    if(project_field) { x=projection.x(x); y=projection.y(y); }
    if(unsigned(x) >= 240 || y < clip_top || y >= clip_bottom) return;
    auto* bytes = reinterpret_cast<uint8_t*>(screen_tiles);
    bytes[((y >> 3)*30 + (x >> 3))*64 + (y & 7)*8 + (x & 7)] = uint8_t(color);
}

void Render::rect(int x, int y, int w, int h, int color) {
    if(w<=0 || h<=0) return;
    if(project_field) {
        const int right=projection.x(x+w-1)+1, bottom=projection.y(y+h-1)+1;
        x=projection.x(x); y=projection.y(y);
        project_field=false;
        rect(x,y,right-x,bottom-y,color);
        project_field=true;
        return;
    }
    const int x2 = minimum(240, x+w), y2 = minimum(clip_bottom, y+h);
    x = maximum(0,x); y = maximum(clip_top,y);
    if(x >= x2 || y >= y2) return;
    const uint32_t packed = unsigned(color)*0x01010101;
    if(x==0 && x2==240 && y2-y>=8) {
        const int middle_top=(y+7)&~7, middle_bottom=y2&~7;
        if(middle_top<middle_bottom) {
            if(y<middle_top) rect(0,y,240,middle_top-y,color);
            bn::memory::set_words(packed,(middle_bottom-middle_top)*60,&screen_tiles[(middle_top>>3)*60].data[0]);
            if(middle_bottom<y2) rect(0,middle_bottom,240,y2-middle_bottom,color);
            return;
        }
    }
    const int aligned_start = minimum(x2,(x+3)&~3);
    const int aligned_end = maximum(aligned_start,x2&~3);
    auto* bytes = reinterpret_cast<uint8_t*>(screen_tiles);
    for(int yy=y; yy<y2; ++yy) {
        const int row_offset = (yy>>3)*30*64 + (yy&7)*8;
        for(int xx=x; xx<aligned_start; ++xx)
            bytes[row_offset+(xx>>3)*64+(xx&7)] = uint8_t(color);
        auto* words = reinterpret_cast<uint32_t*>(bytes+row_offset);
        for(int xx=aligned_start; xx<aligned_end; xx+=4)
            words[(xx>>3)*16+((xx&7)>>2)] = packed;
        for(int xx=aligned_end; xx<x2; ++xx)
            bytes[row_offset+(xx>>3)*64+(xx&7)] = uint8_t(color);
    }
}

// Dithered vertical sheen. The four ramp tones in palette slots 246-249 are
// mixed with a 4x4 Bayer matrix, so a panel reads as a smooth gradient while
// the hardware still shows at most four colours. Rows and columns are phased
// on absolute screen coordinates, so neighbouring windows shade consistently.
// The Bayer pattern repeats every four columns, so the aligned middle of each
// row is a single repeated 32-bit value instead of eight byte stores.
// The sheen fill and the menu arrow run on input-driven menus (and the 5 Hz map
// HUD), not per combat frame, so they live in ROM and keep the IWRAM budget for
// the battle and map hot paths.
__attribute__((section(".text")))
void Render::ui_gradient(int x, int y, int w, int h, int top, int bottom, int ramp_kind) {
    // Ramp 0 is the warm paper family of light detail panels; ramp 1 is the deep
    // navy family of the new theme's menus.
    static const int paper_ramp[4] = {paper_hi, paper_mid, paper_lo, paper_deep};
    static const int navy_ramp[4] = {focus_paper, light_ink, paper, surface};
    const int* ramp = ramp_kind ? navy_ramp : paper_ramp;
    // Retain the old API for layout callers, but use flat materials rather
    // than a dithered sheen. This also removes per-pixel gradient work.
    rect(x,y,w,h,ramp[clamp(bottom,0,3)]);
    if(top!=bottom) rect(x,y,w,1,ramp[clamp(top,0,3)]);
}

// Right-pointing menu arrow, drawn as seven run-length rows.
__attribute__((section(".text")))
void Render::ui_cursor(int x, int y, int color) {
    static const uint8_t widths[7] = {1,2,3,4,3,2,1};
    for(int row = 0; row < 7; ++row) rect(x, y + row, widths[row], 1, color);
}

void Render::line(int x, int y, int x2, int y2, int color) {
    if(project_field) {
        project_field=false;
        line(projection.x(x),projection.y(y),projection.x(x2),projection.y(y2),color);
        project_field=true;
        return;
    }
    if(maximum(x,x2)<0 || minimum(x,x2)>=240 ||
       maximum(y,y2)<clip_top || minimum(y,y2)>=clip_bottom) return;
    int dx = x2 - x; if(dx < 0) dx = -dx;
    int dy = y2 - y; if(dy > 0) dy = -dy;
    int sx = x < x2 ? 1 : -1, sy = y < y2 ? 1 : -1;
    int error = dx + dy;
    for(;;) {
        pixel(x,y,color);
        if(x == x2 && y == y2) break;
        int twice = 2 * error;
        if(twice >= dy) { error += dy; x += sx; }
        if(twice <= dx) { error += dx; y += sy; }
    }
}

void Render::text(int x, int y, const char* string, int color) {
    auto* bytes = reinterpret_cast<const unsigned char*>(string);
    while(*bytes && x < 238) {
        uint32_t cp = *bytes++;
        if(cp >= 0xe0 && cp < 0xf0 && bytes[0] && bytes[1]) { cp = ((cp & 15) << 12) | ((bytes[0] & 63) << 6) | (bytes[1] & 63); bytes += 2; }
        else if(cp >= 0xc0 && cp < 0xe0 && bytes[0]) { cp = ((cp & 31) << 6) | (bytes[0] & 63); ++bytes; }
        const auto* g = glyph(cp);
        if(g) {
            auto* output = reinterpret_cast<uint8_t*>(screen_tiles);
            for(int row = maximum(0,-y); row < minimum(12,160-y); ++row) {
                const int yy=y+row;
                const int offset=(yy>>3)*30*64+(yy&7)*8;
                const unsigned bits=g->rows[row];
                for(int col=maximum(0,-x); col<minimum(int(g->width),240-x); ++col) {
                    if(bits & (1u<<col)) {
                        const int xx=x+col;
                        output[offset+(xx>>3)*64+(xx&7)] = uint8_t(color);
                    }
                }
            }
            x += g->width;
        } else x += 12;
    }
}

void Render::number(int x, int y, int value, int color) {
    char buffer[12];
    int end = 11;
    buffer[end] = 0;
    bool negative = value < 0;
    if(negative) value = -value;
    do { buffer[--end] = char('0' + value % 10); value /= 10; } while(value && end > 1);
    if(negative) buffer[--end] = '-';
    text(x, y, buffer + end, color);
}

void Render::bar(int x, int y, int w, int value, int total, int color) {
    rect(x,y,w,4,ink);
    rect(x,y,clamp(total ? value * w / total : 0,0,w),4,color);
}













void Render::portrait(int x, int y, int general, int color) {
    rect(x,y,24,28,ink);
    if(unsigned(general) < general_count) {
        for(int py = 0; py < 24; ++py) for(int px = 0; px < 24; ++px) {
            const uint8_t packed = face_pixels[general][(py * 24 + px) >> 1];
            const int index = (packed >> ((px & 1) * 4)) & 15;
            pixel(x + px, y + py, index);
        }
    } else {
        rect(x+6,y+3,12,5,color);
        rect(x+8,y+8,8,9,tan);
        pixel(x+9,y+11,ink); pixel(x+14,y+11,ink);
        rect(x+5,y+19,14,7,color);
    }
}

void Render::small_portrait(int x, int y, int general, int frame_color) {
    rect(x,y,12,14,frame_color);
    rect(x+1,y+1,10,12,ink);
    if(unsigned(general) >= general_count) return;
    for(int py = 0; py < 12; ++py) for(int px = 0; px < 10; ++px) {
        const int sx = px * 24 / 10, sy = py * 2;
        const uint8_t packed = face_pixels[general][(sy * 24 + sx) >> 1];
        const int index = (packed >> ((sx & 1) * 4)) & 15;
        if(index) pixel(x+1+px,y+1+py,index);
    }
}

void Render::bitmap(int x, int y, int w, int h, const uint8_t* pixels) {
    const int left=maximum(0,-x), right=minimum(w,240-x);
    const int top=maximum(0,clip_top-y), bottom=minimum(h,clip_bottom-y);
    if(left>=right || top>=bottom) return;
    auto* bytes = reinterpret_cast<uint8_t*>(screen_tiles);
    for(int py=top;py<bottom;++py) {
        const int yy=y+py;
        const int row_offset=(yy>>3)*30*64+(yy&7)*8;
        for(int px=left;px<right;++px) {
            const int source_index=py*w+px;
            const int color=(pixels[source_index>>1]>>((source_index&1)*4))&15;
            if(color) {
                const int xx=x+px;
                bytes[row_offset+(xx>>3)*64+(xx&7)]=uint8_t(color);
            }
        }
    }
}

void Render::fullscreen_bitmap(const uint8_t* pixels) {
    map_ui_active=false;
    for(int y = 0; y < 160; ++y) {
        const int tile_row = (y >> 3) * 30;
        for(int tile_x = 0; tile_x < 30; ++tile_x) {
            const int byte = (y * 240 + tile_x * 8) >> 1;
            const uint32_t packed = uint32_t(pixels[byte]) |
                (uint32_t(pixels[byte+1]) << 8) |
                (uint32_t(pixels[byte+2]) << 16) |
                (uint32_t(pixels[byte+3]) << 24);
            legacy_row(tile_row+tile_x,y & 7,packed);
        }
    }
}

__attribute__((section(".text")))
void Render::ui_bitmap(int x,int y,int w,int h,const uint8_t* pixels) {
    // The title is opaque and word-aligned. Copy tile rows in pairs of words
    // instead of doing transparency tests and tile addressing 38,400 times.
    if(pixels==silk_title_pixels && x==0 && y==0 && w==240 && h==160) {
        auto* output=reinterpret_cast<uint32_t*>(screen_tiles);
        const auto* source=reinterpret_cast<const uint32_t*>(pixels);
        for(int ty=0;ty<20;++ty) for(int tx=0;tx<30;++tx) {
            const auto* input=source+ty*480+tx*2;
            auto* target=output+(ty*30+tx)*16;
            for(int row=0;row<8;++row) {
                target[row*2]=input[row*60];
                target[row*2+1]=input[row*60+1];
            }
        }
        return;
    }
    auto* bytes=reinterpret_cast<uint8_t*>(screen_tiles);
    for(int py=maximum(0,-y);py<minimum(h,160-y);++py) {
        const int yy=y+py, offset=(yy>>3)*30*64+(yy&7)*8;
        for(int px=maximum(0,-x);px<minimum(w,240-x);++px) {
            const int color=pixels[py*w+px];
            if(color) { const int xx=x+px; bytes[offset+(xx>>3)*64+(xx&7)]=uint8_t(color); }
        }
    }
}

// Infrequent menu glyphs yield fast RAM to the continuous music decoder.
__attribute__((section(".text")))
void Render::ui_glyph(int x,int y,const UiGlyph& glyph,int height,int color) {
    const int left=maximum(0,x), right=minimum(240,x+int(glyph.width));
    const int top=maximum(0,-y), bottom=minimum(height,160-y);
    if(left>=right || top>=bottom) return;
    // Expand four coverage bits at once. Chinese menu text used to walk every
    // transparent pixel as well as every painted pixel from slow cartridge ROM.
    static constexpr uint32_t masks[16]={
        0,0xff,0xff00,0xffff,0xff0000,0xff00ff,0xffff00,0xffffff,
        0xff000000,0xff0000ff,0xff00ff00,0xff00ffff,
        0xffff0000,0xffff00ff,0xffffff00,0xffffffff
    };
    const uint32_t ink=unsigned(color)*0x01010101;
    const unsigned width_mask=(1u<<glyph.width)-1;
    const int first_word=left&~3;
    for(int row=top;row<bottom;++row) {
        const int yy=y+row;
        auto* words=reinterpret_cast<uint32_t*>(screen_tiles)+(yy>>3)*30*16+(yy&7)*2;
        unsigned bits=glyph.rows[row]&width_mask;
        const int shift=first_word-x;
        bits=shift>=0 ? bits>>shift : bits<<(-shift);
        for(int xx=first_word;xx<right && bits;xx+=4,bits>>=4) {
            const uint32_t mask=masks[bits&15];
            if(mask) {
                auto& word=words[(xx>>3)*16+((xx&7)>>2)];
                word=(word&~mask)|(ink&mask);
            }
        }
    }
}

void Render::character(int x, int y, int w, int h, const uint8_t* pixels, bool flip) {
    if(project_field) { scaled_sprite(x,y,w,h,pixels,flip,-1); return; }
    const int left=maximum(0,-x), right=minimum(w,240-x);
    const int top=maximum(0,battle_view_top-y), bottom=minimum(h,battle_view_bottom-y);
    if(left>=right || top>=bottom) return;
    auto* bytes=reinterpret_cast<uint8_t*>(screen_tiles);
    for(int py=top;py<bottom;++py) {
        const int yy=y+py;
        const int row_offset=(yy>>3)*30*64+(yy&7)*8;
        const uint8_t* source=pixels+py*w;
        int px=left;
        auto edge_pixel = [&] {
            const int color=source[flip ? w-1-px : px];
            if(color) {
                const int xx=x+px;
                bytes[row_offset+(xx>>3)*64+(xx&7)]=uint8_t(color);
            }
            ++px;
        };
        while(px<right && ((x+px)&3)) edge_pixel();
        if(!(w&3) && !(reinterpret_cast<uintptr_t>(source)&3)) {
            const auto* input=reinterpret_cast<const uint32_t*>(source);
            for(;px+4<=right;px+=4) {
                const int start=flip ? w-px-4 : px;
                const int shift=(start&3)*8;
                uint32_t packed=input[start>>2]>>shift;
                if(shift) packed |= input[(start>>2)+1]<<(32-shift);
                if(flip) packed=__builtin_bswap32(packed);
                if(!packed) continue;
                // Build one alpha mask for four pixels; zero is transparent.
                const uint32_t high=(packed|((packed&0x7f7f7f7f)+0x7f7f7f7f))&0x80808080;
                const uint32_t mask=(high>>7)*255;
                const int xx=x+px;
                auto* output=reinterpret_cast<uint32_t*>(bytes+row_offset+(xx>>3)*64+(xx&7));
                *output=mask==0xffffffff ? packed : (packed|(*output&~mask));
            }
        }
        while(px<right) edge_pixel();
    }
}

__attribute__((section(".text")))
void Render::character_white_light(int x,int y,int w,int h,
                                   const uint8_t* pixels,bool flip,
                                   bool half_mix) {
    if(project_field) { scaled_sprite(x,y,w,h,pixels,flip,half_mix ? 1 : 0); return; }
    if(const auto* rows=effect_sprite_rows(pixels,w,h)) {
        effect_white_light_rows(x,y,w,h,pixels,rows,half_mix,flip);
        return;
    }
    constexpr int effect_palette_first=118;
    constexpr int effect_palette_size=48;
    // Sequence4205 sets the original 0..16 mixer to8. The native mixer table
    // scales the sprite color to half intensity; the white-light operation
    // then adds that scaled source to the destination with channel saturation.
    const int left=maximum(0,-x), right=minimum(w,240-x);
    const int top=maximum(0,battle_view_top-y), bottom=minimum(h,battle_view_bottom-y);
    if(left>=right || top>=bottom) return;
    auto* bytes=reinterpret_cast<uint8_t*>(screen_tiles);
    for(int py=top;py<bottom;++py) {
        const int yy=y+py;
        const int row_offset=(yy>>3)*30*64+(yy&7)*8;
        const uint8_t* source=pixels+py*w;
        if(!flip && !half_mix) {
            int first=left,last=right;
            while(first<last && !source[first]) ++first;
            while(last>first && !source[last-1]) --last;
            for(int px=first;px<last;) {
                const int xx=x+px;
                const int count=minimum(last-px,8-(xx&7));
                blend_effect_span(bytes+row_offset+(xx>>3)*64+(xx&7),
                                  source+px,count,&sg::additive_palette[0][0][0]);
                px+=count;
            }
            continue;
        }
        for(int px=left;px<right;++px) {
            const uint8_t src=source[flip ? w-1-px : px];
            if(!src) continue;
            const int xx=x+px;
            uint8_t& output=bytes[row_offset+(xx>>3)*64+(xx&7)];
            if(!half_mix &&
               (src<effect_palette_first || src>=effect_palette_first+effect_palette_size)) {
                output=src;
                continue;
            }
            const int effect_index=src-effect_palette_first;
            const bool cacheable=effect_index>=0 && effect_index<effect_palette_size;
            if(cacheable) {
                output=sg::additive_palette[half_mix ? 1 : 0][effect_index][output];
                continue;
            }
            const bn::color source_color=colors[src];
            const bn::color destination_color=colors[output];
            int red,green,blue;
            if(!half_mix) {
                red=minimum(31,source_color.red()+destination_color.red());
                green=minimum(31,source_color.green()+destination_color.green());
                blue=minimum(31,source_color.blue()+destination_color.blue());
            } else {
                red=minimum(31,source_color.red()*127/255+destination_color.red());
                green=minimum(31,source_color.green()*127/255+destination_color.green());
                blue=minimum(31,source_color.blue()*127/255+destination_color.blue());
            }
            int best_index=0;
            int best_distance=0x7fffffff;
            for(int candidate=0;candidate<=165;++candidate) {
                const bn::color candidate_color=colors[candidate];
                const int dr=red-candidate_color.red();
                const int dg=green-candidate_color.green();
                const int db=blue-candidate_color.blue();
                const int distance=dr*dr+dg*dg+db*db;
                if(distance<best_distance) {
                    best_distance=distance;
                    best_index=candidate;
                }
            }
            output=uint8_t(best_index);
        }
    }
}














__attribute__((section(".text")))
void Render::effect_white_light_rows(int x,int y,int w,int h,const uint8_t* pixels,
                                     const uint8_t* row_spans,bool half_mix,bool flip) {
    if(project_field) { scaled_sprite(x,y,w,h,pixels,flip,half_mix ? 1 : 0); return; }
    // Imported effect-only pixels are either transparent or palette118..165.
    // ROM row spans omit the empty border; the same LUT preserves exact mixing.
    const int left=maximum(0,-x), right=minimum(w,240-x);
    const int top=maximum(0,battle_view_top-y), bottom=minimum(h,battle_view_bottom-y);
    if(left>=right || top>=bottom) return;
    const auto& table=sg::additive_palette[half_mix ? 1 : 0];
    auto* bytes=reinterpret_cast<uint8_t*>(screen_tiles);
    for(int py=top;py<bottom;++py) {
        const int first=maximum(left,flip ? w-int(row_spans[py*2+1]) : int(row_spans[py*2]));
        const int last=minimum(right,flip ? w-int(row_spans[py*2]) : int(row_spans[py*2+1]));
        const int yy=y+py;
        const int row_offset=(yy>>3)*30*64+(yy&7)*8;
        const uint8_t* source=pixels+py*w;
        for(int px=first;px<last;) {
            const int xx=x+px;
            const int count=minimum(last-px,8-(xx&7));
            if(!half_mix && !flip)
                blend_effect_span(bytes+row_offset+(xx>>3)*64+(xx&7),source+px,count,&table[0][0]);
            else blend_effect_strided_span(bytes+row_offset+(xx>>3)*64+(xx&7),
                source+(flip ? w-1-px : px),count,&table[0][0],flip ? -1 : 1);
            px+=count;
        }
    }
}

void Render::magic_frame(int script, int frame, int x, int y, bool flip) {
    if(script==3900) {
        // Sequence3900 holds M009ThunderA1 for two updates, then switches to
        // NULSHAPE. The animated A3/A4 art belongs to sequences3910-3913.
        const int lightning_frame=0;
        // The target is the bolt's foot, not the source bitmap's top-left.
        character_white_light(visual::centered_left(x,sg::magic_lightning_width),
                              visual::grounded_top(y,sg::magic_lightning_height),
                              sg::magic_lightning_width,sg::magic_lightning_height,
                              sg::magic_lightning_sprites[lightning_frame],false);
        return;
    }
    if(script>=2645 && script<=2647) {
        // GLOBAL4600 mode0 creates sequence4601/M016Man2; modes1/2
        // create sequence4600/M016Man1.
        character_white_light(visual::centered_left(x,sg::magic_clone_slash_width),
                  visual::grounded_top(y,sg::magic_clone_slash_height),
                  sg::magic_clone_slash_width,sg::magic_clone_slash_height,
                  script==2645 ? sg::magic_clone_slash_alt_sprite :
                                 sg::magic_clone_slash_sprite,flip);
        return;
    }
    if(script>=2639 && script<=2641) {
        // Things.ini sequence4400 cycles the eight M014 sword angles every
        // two object updates for each spawned sword. The source has Space=0;
        // retain each fitted bitmap's top-left at the raw object origin,
        // accounting for transparent canvas padding. The native draw call
        // receives x/y/image/z only and does not mirror the bitmap by facing.
        const int sword_frame=clamp(frame,0,7);
        const int x_offset=sg::magic_sword_sweep_x_offset[sword_frame];
        character_white_light(x-x_offset,
                  y-sg::magic_sword_sweep_y_offset[sword_frame],
                  sg::magic_sword_sweep_size,sg::magic_sword_sweep_size,
                  sg::magic_sword_sweep_sprites[sword_frame],false);
        return;
    }
    if((script>=2633 && script<=2635) || script==2660) {
        magic_m012_frame(script,frame,x,y);
        return;
    }
    if(script>=2630 && script<=2632) {
        // Things.ini sequences 4100/4101 use M011WaveA/B; the traveling
        // collision object uses the animated A sequence. Native objects have
        // Space=0 and pass their raw x/y to the image renderer, so keep the
        // fitted source image origin at that object position, compensating
        // for the transparent padding added to the fixed GBA canvas.
        const int wave_frame=frame&1;
        const int x_offset=sg::magic_m011_wave_x_offset[wave_frame];
        character_white_light(x-x_offset,
                  y-12-sg::magic_m011_wave_height/2,
                  sg::magic_m011_wave_width,sg::magic_m011_wave_height,
                  sg::magic_m011_wave_sprites[wave_frame],false);
        return;
    }
    if(script>=2642 && script<=2644) {
            // MAGIC01 4501 creates Things sequence4500 for every projectile;
            // that sequence is the static M015DragonA1 image.
            const int dragon_frame=0;
        const int core_x_offset=sg::magic_dragon_core_x_offset[dragon_frame];
        character_white_light(x-core_x_offset,
                  y-sg::magic_dragon_core_y_offset[dragon_frame],
                  sg::magic_dragon_sprite_size,
                  sg::magic_dragon_sprite_size,
                  sg::magic_dragon_sprites[dragon_frame],false);
        return;
    }
    if(script==2618 || script==2622 || script==2623) {
        // Things.ini 3702/3703/3802/3803 hold the static M007Ston1 image
        // throughout the collidable stone's flight.
        character(x-sg::magic_m007_stone_size/2,
                  y-sg::magic_m007_stone_size/2,
                  sg::magic_m007_stone_size,sg::magic_m007_stone_size,
                  sg::magic_m007_stone_sprite,false);
        return;
    }
    int type=0;
    if(script==2666 || script==2667 || (script>=2669 && script<=2673)) type=1;
    else if(original::magic_uses_arrow_volley(script) || script==3100 || script==4200 || script==2660 || script==2664) type=2;
    else if(original::lightning_strike_count(script) || script==3900) type=3;
    else if((script>=2630 && script<=2632) || script==4100) type=4;
    else if(original::sword_wave_projectile_count(script) || (script>=2633 && script<=2635) || script==2646 || script==4400 || script==4600) type=5;
    else if((script>=2642 && script<=2644) || script==4500) type=6;
    else if(script==2618 || script==2622 || script==2623 || script==3700 || script==3500 || original::minefield_count(script)) type=7;
    character(x-24,y-48,48,48,battle_effects[type][frame%3],flip);
}

void Render::warbar_frame(int kind, int x, int y) {
    // A projectile is one arrow, rather than a 132-pixel imported strip.
    const int direction=kind==2 || kind==4 || kind==7 ? -1 : 1;
    const int tip=x+direction*5;
    line(x-direction*5,y,tip,y,pale);
    line(tip-direction*3,y-2,tip,y,white);
    line(tip-direction*3,y+2,tip,y,white);
    pixel(x-direction*4,y-1,brown);
    pixel(x-direction*4,y+1,brown);
}

void Render::battle_arrow(int x,int y,int velocity_x,int velocity_y) {
    // Direction is the actual world velocity, never a soldier/skill ID.
    // Hardware mirroring flips the glyph and its trajectory together once.
    const int variant=(velocity_y<0 ? 0 : velocity_y>0 ? 2 : 4)+(velocity_x>0 ? 1 : 0);
    character(x-7,y-7,16,16,visual::projectile_arrows[variant].pixels,false);
}

void Render::battle_frame() {
    faction_palette(false);
    rect(0,0,240,battle_view_top,surface);
    rect(0,0,240,1,ochre);
    rect(3,1,112,battle_view_top-1,paper); rect(3,0,112,1,indigo);
    rect(3,1,2,battle_view_top-2,text_ink);
    rect(125,1,112,battle_view_top-1,paper); rect(125,0,112,1,warning_ink);
    rect(235,1,2,battle_view_top-2,surface);
    rect(116,12,8,17,pale_paper);
    text(117,14,"VS",ochre);
}

void Render::map(const Game& game, int selected, bool paused, int selection_color, const char* notice, bool zoomed, int speed, bool compact, int focused_army) {
    faction_palette(true);
    selected=clamp(selected,0,city_count-1);
    const int view_height=compact ? map_compact_field_height : map_field_height;
    const int view_bottom=map_field_top+view_height;
    const int view_center=map_field_top+view_height/2;
    const bool focus_active=focused_army>=0 && focused_army<max_armies && game.campaign.armies[focused_army].active;
    MapPoint world_center={city_defs[selected].x,city_defs[selected].y};
    if(zoomed && focus_active) {
        const auto p=army_position(game.campaign,focused_army);
        // Eight world pixels are at most three screen pixels. Quantization
        // avoids rebuilding the terrain for subpixel movement, without ever
        // letting the selected marching army leave the local viewport.
        world_center={(p.x+4)/8*8,(p.y+4)/8*8};
    }
    // Cache by the actual world camera, not the city from the first selection.
    // Bit 23 separates compact target maps from full-height strategic maps.
    const int center=(zoomed ? (world_center.x<<11)|world_center.y : -1)+(compact ? 1<<23 : 0);
    bool rebuild=cached_map_center!=center || cached_map_zoom!=zoomed;
    for(int i=0;i<city_count;++i)
        rebuild |= cached_map_owners[i]!=game.campaign.cities[i].king;
    const auto& c = game.campaign.cities[selected];
    int count=0;
    for(const auto& o:game.campaign.officers)
        count+=o.city==selected && o.army<0 && o.status==Status::employed && o.king==c.king;
    static int hud_selected=-1,hud_year=-1,hud_month=-1,hud_phase=-1,hud_speed=-1,hud_count=-1,hud_king=-2,hud_defense=-1;
    static bool hud_paused=false;
    static int hud_focused_army=-1;
    static uint32_t hud_money=0,hud_population=0;
    const bool hud_dirty=focused_army>=0 || hud_focused_army!=focused_army || !map_ui_active || rebuild || hud_selected!=selected || hud_year!=game.campaign.year ||
        hud_month!=game.campaign.month || hud_phase!=int(game.campaign.phase) || hud_speed!=speed || hud_paused!=paused ||
        hud_count!=count || hud_king!=c.king || hud_defense!=c.defense || hud_money!=c.money || hud_population!=c.population;
    // Only clear the strips outside the cached field on ordinary redraws.
    // This also restores upright tiles after leaving combat.
    battle_orientation(false); combat_active=false; upload_full=true;
    if(rebuild) {
        clear(paper);

        // Add decorative frame for domestic phase map
        if(game.campaign.phase == Phase::domestic) {
            // Top decorative border
            rect(0, map_field_top - 1, 240, 1, gold_lo);
            // Bottom decorative border
            rect(0, view_bottom, 240, 1, gold_lo);

            // Corner decorations
            rect(2, map_field_top + 2, 4, 1, paper_hi);
            rect(2, map_field_top + 2, 1, 4, paper_hi);
            rect(234, map_field_top + 2, 4, 1, paper_hi);
            rect(237, map_field_top + 2, 1, 4, paper_hi);
        }

        const int center_x=world_center.x*terrain_zoom_width/1200;
        const int center_y=world_center.y*terrain_zoom_height/950;
        const int stride=(terrain_zoom_width+7)/8;
        for(int y=0;y<view_height;++y) {
            const int screen_y=y+map_field_top;
            const int source_y=center_y+screen_y-view_center;
            const int overview_y=y*terrain_overview_height/view_height;
            for(int tile_x=0;tile_x<30;++tile_x) {
                uint32_t packed=terrain_overview_rows[overview_y*30+tile_x];
                if(zoomed) {
                    const int source_x=center_x+tile_x*8-120;
                    packed=0x11111111;
                    if(unsigned(source_y)<terrain_zoom_height) {
                        if(source_x>=0 && source_x+7<terrain_zoom_width) {
                            const int source_tile=source_y*stride+(source_x>>3);
                            const int shift=(source_x&7)*4;
                            packed=terrain_zoom_rows[source_tile]>>shift;
                            if(shift) packed|=terrain_zoom_rows[source_tile+1]<<(32-shift);
                        } else if(source_x>-8 && source_x<terrain_zoom_width) {
                            // Only the two horizontal boundary tiles need
                            // per-pixel clipping. Interior tiles are word reads.
                            packed=0;
                            for(int px=0;px<8;++px) {
                                const int sx=source_x+px;
                                const int value=unsigned(sx)<terrain_zoom_width ?
                                    int((terrain_zoom_rows[source_y*stride+(sx>>3)]>>((sx&7)*4))&15) : 1;
                                packed|=uint32_t(value)<<(px*4);
                            }
                        }
                    }
                }
                map_row((screen_y>>3)*30+tile_x,screen_y&7,packed);
            }
        }
        clip_top=map_field_top; clip_bottom=view_bottom;
        for(int i=0;i<city_count;++i) {
            const int x=map_view_x(city_defs[i].x,selected,zoomed,&world_center), y=map_view_y(city_defs[i].y,selected,zoomed,view_height,&world_center);
            if(x>=3 && x<=236 && y>=map_field_top+1 && y<view_bottom-1) {
                const int owner=game.campaign.cities[i].king;
                const char* name=owner>=0 ? king_defs[game.campaign.scenario][owner].name : nullptr;
                int letter_ink=edge_light;
                if(owner>=0) {
                    const auto& pigment=faction_colors[owner];
                    const int light=54*pigment.red()+183*pigment.green()+19*pigment.blue();
                    if(light>=15*256) letter_ink=edge_dark;
                }
                map_city(x,y,king_ink(owner,game.campaign.player),zoomed,owner>=0,name,letter_ink);
            }
            cached_map_owners[i]=game.campaign.cities[i].king;
        }
        clip_top=0; clip_bottom=160;
        bn::memory::copy(screen_tiles[map_cache_first_row*60],map_cache_tile_rows*60,map_backdrop[0]);
        cached_map_center=center; cached_map_zoom=zoomed;
    } else {
        // Restore only field scanlines, not the HUD sharing its boundary tiles.
        const int first_full=(map_field_top+7)/8, last_full=view_bottom/8;
        bn::memory::copy(map_backdrop[(first_full-map_cache_first_row)*60],
                         (last_full-first_full)*60,screen_tiles[first_full*60]);
        const auto restore_edge=[&](int y) {
            for(int tile=0;tile<30;++tile) for(int half=0;half<2;++half)
                tile_word((y/8)*30+tile,y&7,half)=
                    map_backdrop[(y/8-map_cache_first_row)*60+tile*2+((y&7)>>2)].data[((y&3)*2)+half];
        };
        for(int y=map_field_top;y<first_full*8;++y) restore_edge(y);
        for(int y=last_full*8;y<view_bottom;++y) restore_edge(y);
        if(hud_dirty) { rect(0,0,240,map_field_top,paper); rect(0,view_bottom,240,160-view_bottom,paper); }
    }
    clip_top=map_field_top; clip_bottom=view_bottom;
    const int selected_x=map_view_x(city_defs[selected].x,selected,zoomed,&world_center);
    const int selected_y=map_view_y(city_defs[selected].y,selected,zoomed,view_height,&world_center);
    const int selection_ink=selection_color>=0 ? selection_color : gold_hi;
    // In zoom, focus the ownership flag rather than surrounding nearby cities.
    // Match map_city's edge placement; unowned cities retain the gate anchor.
    const bool flag_focus=zoomed && c.king>=0;
    const auto flag=map_flag_origin(selected_x,selected_y,map_field_top);
    const int focus_x=flag_focus ? flag.x+map_flag_width/2 : selected_x;
    const int focus_y=flag_focus ? flag.y+map_flag_height/2 : selected_y;
    const int frame_x=zoomed ? 9 : 5, frame_y=zoomed ? 8 : 5, tick=2;
    // Corner brackets leave both the ownership square and the flag untouched.
    if(!focus_active) for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2) {
        const int cx=focus_x+sx*frame_x, cy=focus_y+sy*frame_y;
        rect(minimum(cx,cx-sx*tick)-1,cy-1,tick+3,3,edge_dark);
        rect(cx-1,minimum(cy,cy-sy*tick)-1,3,tick+3,edge_dark);
        line(cx,cy,cx-sx*tick,cy,selection_ink);
        line(cx,cy,cx,cy-sy*tick,selection_ink);
    }
    MapMarkerRect occupied[city_count*2+max_armies];
    int occupied_count=0;
    for(int i=0;i<city_count;++i) {
        const int x=map_view_x(city_defs[i].x,selected,zoomed,&world_center);
        const int y=map_view_y(city_defs[i].y,selected,zoomed,view_height,&world_center);
        if(x<3 || x>236 || y<map_field_top+1 || y>=view_bottom-1) continue;
        if(zoomed) {
            occupied[occupied_count++]={x-5,y-3,x+5,y+4};
            if(game.campaign.cities[i].king>=0) {
                const auto flag=map_flag_origin(x,y,map_field_top);
                occupied[occupied_count++]={flag.x-1,minimum(y-1,flag.y),flag.x+map_flag_width-1,
                    maximum(y+1,flag.y+map_flag_height-1)};
            }
        } else occupied[occupied_count++]={x-3,y-3,x+3,y+3};
    }
    const int city_occupied_count=occupied_count;
    MapMarkerPoint focus_marker={0,0};
    bool focus_marker_visible=false;
    for(const auto& a : game.campaign.armies) if(a.active) {
        const int total_ticks = maximum(1,int(a.total));
        const int progress_ticks = clamp(total_ticks-int(a.remaining),0,total_ticks);
        const int army_index=&a-game.campaign.armies;
        const auto position=army_position(game.campaign,army_index);
        const auto ahead=army_route_position(a,uint32_t(minimum(total_ticks,progress_ticks+8)),army_reverse_geometry(game.campaign,army_index));
        const int x=position.x,y=position.y;
        const int heading_x=ahead.x-x,heading_y=ahead.y-y;
        int mx = map_view_x(x,selected,zoomed,&world_center), my = map_view_y(y,selected,zoomed,view_height,&world_center);
        if(mx<0 || mx>=240 || my<map_field_top || my>=view_bottom) continue;
        // Never relocate moving tokens to a newly available layout slot:
        // that made smooth route progress look like back-and-forth teleporting.
        // City ownership artwork is restored above the tokens below instead.
        occupied[occupied_count++]=army_marker_bounds({mx,my},army_index==focused_army);
        const int sx = heading_x > 0 ? 1 : heading_x < 0 ? -1 : 0;
        const int sy = heading_y > 0 ? 1 : heading_y < 0 ? -1 : 0;
        const int color=king_ink(a.king,game.campaign.player);
        // A pennant on a pole, never another city square. No animated sprite
        // decoding or per-frame allocation is needed for large army counts.
        rect(mx-4,my-6,10,10,surface);
        line(mx-3,my-5,mx-3,my+3,text_ink);
        rect(mx-2,my-5,7,5,color); pixel(mx+4,my-1,surface);
        line(mx-4,my+3,mx-1,my+3,text_ink);
        if(a.garrisoned) { pixel(mx+1,my+2,text_ink);pixel(mx+3,my+2,text_ink); }
        else {
            line(mx-sx,my+2-sy,mx+2*sx,my+2+2*sy,text_ink);
            pixel(mx+sx+sy,my+2+sy-sx,text_ink);
            pixel(mx+sx-sy,my+2+sy+sx,text_ink);
        }
        if(army_index==focused_army) {
            focus_marker={mx,my};focus_marker_visible=true;
        }
    }
    if(zoomed) {
        const auto* cached=reinterpret_cast<const uint8_t*>(map_backdrop);
        auto* output=reinterpret_cast<uint8_t*>(screen_tiles);
        for(int i=0;i<city_occupied_count;++i) {
            const auto& bounds=occupied[i];
            bool covered=false;
            for(int j=city_occupied_count;j<occupied_count;++j)
                if(map_marker_intersects(bounds,occupied[j])) { covered=true;break; }
            if(!covered) continue;
            // Restore only cached city/flag pixels, without redrawing terrain
            // or changing the army's projected world position.
            for(int y=maximum(map_field_top,bounds.top);y<=minimum(view_bottom-1,bounds.bottom);++y)
                for(int x=maximum(0,bounds.left);x<=minimum(239,bounds.right);++x) {
                    const int offset=((y>>3)*30+(x>>3))*64+(y&7)*8+(x&7);
                    output[offset]=cached[offset-map_cache_first_row*30*64];
                }
        }
    }
    if(focus_marker_visible) {
        // Keep the thin selection outline visible even while the token passes
        // behind a city. The token itself never leaves its route anchor.
        const int mx=focus_marker.x,my=focus_marker.y;
        line(mx-6,my-8,mx+7,my-8,cinnabar);
        line(mx-6,my-8,mx-6,my+5,cinnabar);
        line(mx+7,my-8,mx+7,my+5,cinnabar);
        line(mx-6,my+5,mx+7,my+5,cinnabar);
        MapMarkerPoint label;
        if(map_army_label(focus_marker,occupied,occupied_count,map_field_top,view_bottom,label)) {
            rect(label.x-1,label.y-1,60,14,paper);
            const auto& a=game.campaign.armies[focused_army];
            if(a.count) ui_text(label.x,label.y,general_defs[a.generals[0]].name,text_ink,60);
        }
    }
    if(!zoomed) {
        // Focus brackets may cross a neighbouring city's core. Ownership must
        // remain a solid faction pigment, never gold, shadow, or army ink.
        for(int i=0;i<city_count;++i) {
            const int x=map_x(city_defs[i].x),y=map_view_y(city_defs[i].y,selected,false,view_height);
            rect(x-2,y-2,5,5,king_ink(game.campaign.cities[i].king,game.campaign.player));
        }
    }
    clip_top=0; clip_bottom=160;
    rect(3,map_field_top,1,view_height,indigo); rect(236,map_field_top,1,view_height,paper);
    if(hud_dirty) {
    // Command bars reuse the menu sheen so the map, menus and dossiers read as
    // one interface instead of flat rectangles over terrain.
    ui_gradient(0,0,240,18,0,2,1);
    rect(0,0,240,2,indigo);
    rect(0,17,240,1,ochre); rect(0,18,240,1,rule_ink);
    ui_number(7,3,game.campaign.year,text_ink); ui_text(32,3,"年",text_ink);
    ui_number(49,3,game.campaign.month,text_ink); ui_text(62,3,"月",text_ink);
    // Label chips are flat with a bottom keyline only: the 12px glyphs fill
    // rows 3-14, so any top bevel would strike through the text.
    rect(79,3,55,13,focus_paper); rect(79,15,55,1,rule_ink);
    rect(79,3,1,13,rule_ink); rect(133,3,1,13,rule_ink);
    ui_text(84,3,zoomed ? "局部放大" : game.campaign.phase == Phase::domestic ? "内政" : paused ? "暂停" : "大地图",ochre,54);
    if(speed > 1) {
        // Never leave the player guessing whether the clock is accelerated.
        rect(135,3,16,13,focus_paper); rect(135,15,16,1,rule_ink);
        rect(135,3,1,13,rule_ink); rect(150,3,1,13,rule_ink);
        ui_text(137,3,speed >= 4 ? "x4" : "x2",cinnabar,14);
    }
    rect(151,3,82,13,focus_paper); rect(151,15,82,1,rule_ink);
    rect(151,3,1,13,rule_ink); rect(232,3,1,13,rule_ink);
    ui_text(156,3,king_defs[game.campaign.scenario][game.campaign.player].name,indigo);
    rect(3,18,234,1,ochre); rect(3,view_bottom,234,1,rule_ink);
    rect(3,18,1,view_bottom-17,indigo); rect(236,18,1,view_bottom-17,paper);
    const int info_y=view_bottom+1, first_y=info_y+2, second_y=info_y+15;
    ui_gradient(0,info_y,240,160-info_y,1,3,1);
    rect(0,info_y,240,1,ochre); rect(0,info_y+1,240,1,focus_paper);
    ui_text(5,first_y,city_defs[selected].name,ochre,39);
    ui_text(47,first_y,"属",muted_ink,13);
    rect(60,first_y+4,4,4,king_ink(c.king,game.campaign.player));
    ui_text(67,first_y,c.king >= 0 ? king_defs[game.campaign.scenario][c.king].name : "无主",king_ink(c.king,game.campaign.player),60);
    ui_text(132,first_y,c.king<0 ? "无主" : c.king==game.campaign.player ? "我方城池" : "其他势力",c.king==game.campaign.player ? indigo : c.king<0 ? muted_ink : cinnabar,102);
    ui_text(5,second_y,"金",muted_ink); ui_amount(20,second_y,c.money);
    ui_text(75,second_y,"防",muted_ink); ui_number(89,second_y,c.defense);
    ui_text(125,second_y,"人口",muted_ink); ui_amount(151,second_y,c.population);
    ui_text(199,second_y,"将",muted_ink); ui_number(213,second_y,count);
    hud_selected=selected; hud_year=game.campaign.year; hud_month=game.campaign.month;
    hud_phase=int(game.campaign.phase); hud_speed=speed; hud_paused=paused;
    hud_count=count; hud_king=c.king; hud_defense=c.defense; hud_money=c.money; hud_population=c.population;
    hud_focused_army=focused_army;
    }
    map_ui_active=true;
    if(notice && *notice) {
        // Tall enough that the bottom bevel clears a wrapped second text row.
        ui_window(2,view_bottom-34,236,34);
        ui_message(7,view_bottom-29,226,notice,text_ink);
    }
}

__attribute__((section(".text")))
void Render::destinations(const Game& game, int origin, int selected, const char* notice, int speed, bool zoomed) {
    int options[city_count];
    int count=0, nearby[city_count];
    const uint64_t reachable=game.reachable_city_mask(origin);
    const int nearby_count=game.neighbors(origin,nearby);
    for(int i=0;i<nearby_count;++i)
        if((reachable>>nearby[i])&1) options[count++]=nearby[i];
    for(int target=0;target<city_count;++target) if(target!=origin && ((reachable>>target)&1)) {
        bool listed=false;
        for(int i=0;i<count;++i) listed |= options[i]==target;
        if(!listed) options[count++]=target;
    }
    if((reachable>>origin)&1) options[count++]=origin;
    target_cities(game,origin,options,count,selected,true,notice,speed,zoomed);
}

__attribute__((section(".text")))
void Render::target_cities(const Game& game,int origin,const int* options,int count,
                          int selected,bool dispatch,const char* notice,int speed,bool zoomed,int army_index) {
    // Both demake expedition and army commands open SelectTargetCity on the map.
    // Blue marks the route origin; gold marks the currently selected target.
    const int target=count>0 ? options[clamp(selected,0,count-1)] : origin;
    const int center=zoomed ? target : origin;
    map(game,center,true,zoomed ? int(gold_hi) : int(indigo),nullptr,zoomed,speed,true);
    const auto view_x=[&](int world_x) { return map_view_x(world_x,center,zoomed); };
    const auto view_y=[&](int world_y) { return map_view_y(world_y,center,zoomed,map_compact_field_height); };
    rect(79,3,72,13,focus_paper);
    ui_text(84,3,army_index>=0 ? "部队改道" : "从",muted_ink,65);
    if(army_index<0) ui_text(98,3,city_defs[origin].name,indigo,52);
    map_ui_active=false;
    uint64_t reachable=0;
    for(int i=0;i<count;++i) reachable|=uint64_t(1)<<options[i];
    const uint64_t adjacent=adjacent_reachable_cities(origin,reachable);
    clip_top=map_field_top;clip_bottom=115;
    if(count > 0) {
        selected = clamp(selected,0,count-1);
        const int x = view_x(city_defs[target].x), y = view_y(city_defs[target].y);
        const int origin_x = view_x(city_defs[origin].x), origin_y = view_y(city_defs[origin].y);
        auto segment=[&](int route_x,int route_y,int end_x,int end_y) {
        int dx = end_x > route_x ? end_x - route_x : route_x - end_x;
        int sx = route_x < end_x ? 1 : -1;
        int dy = -(end_y > route_y ? end_y - route_y : route_y - end_y);
        int sy = route_y < end_y ? 1 : -1;
        int error = dx + dy, step = 0;
        for(;;) {
            if(step % 4 == 0) pixel(route_x,route_y,step % 8 == 0 ? ochre : text_ink);
            if(route_x == end_x && route_y == end_y) break;
            const int twice = 2 * error;
            if(twice >= dy) { error += dy; route_x += sx; }
            if(twice <= dx) { error += dx; route_y += sy; }
            ++step;
        }
        };
        int last_x=origin_x,last_y=origin_y;
        int route_origin=origin;
        Army preview;uint8_t pending=0;
        if(army_index>=0 && game.preview_army_redirect(army_index,target,preview,pending)) {
            const bool reverse=(pending&0x80)!=0;
            const int elapsed=int(preview.total)-int(preview.remaining);
            const auto live=army_position(game.campaign,army_index);
            last_x=view_x(live.x);last_y=view_y(live.y);
            // Sample only the remaining part of the live edge, in the same
            // direction/geometry as the actual redirect. Never draw from an
            // arbitrarily chosen nearby city or retrace an already walked road.
            for(int tick=elapsed;tick<int(preview.total);) {
                tick=minimum(int(preview.total),tick+4);
                const auto p=army_route_position(preview,uint32_t(tick),reverse);
                const int px=view_x(p.x),py=view_y(p.y);
                segment(last_x,last_y,px,py);last_x=px;last_y=py;
            }
            route_origin=preview.target;
            rect(view_x(live.x)-2,view_y(live.y)-2,5,5,indigo);
        }
        int cities[city_count];
        const int path_count=map_route_cities(route_origin,target,cities,city_count);
        for(int edge=1;edge<path_count;++edge) {
            const auto& route=city_routes[cities[edge-1]][cities[edge]];
            for(int i=0;i<route.count;++i) {
                const auto& point=city_route_points[route.first+i];
                const int px=view_x(point.x),py=view_y(point.y);
                segment(last_x,last_y,px,py);last_x=px;last_y=py;
            }
            const int px=view_x(city_defs[cities[edge]].x),py=view_y(city_defs[cities[edge]].y);
            segment(last_x,last_y,px,py);last_x=px;last_y=py;
        }
        if(path_count>0) segment(last_x,last_y,x,y);
    }
    for(int i=0;i<city_count;++i) if((reachable>>i)&1) {
        int x=view_x(city_defs[i].x),y=view_y(city_defs[i].y);
        if(x<0 || x>=240 || y<map_field_top || y>=115) continue;
        if(zoomed && game.campaign.cities[i].king>=0) {
            const auto flag=map_flag_origin(x,y,map_field_top);
            x=flag.x+map_flag_width/2;y=flag.y+map_flag_height/2;
        }
        const int rx=zoomed ? 9 : 4,ry=zoomed ? 8 : 4;
        if(i==origin || ((adjacent>>i)&1)) {
            rect(x-rx,y-ry,rx*2+1,1,indigo);rect(x-rx,y+ry,rx*2+1,1,indigo);
            rect(x-rx,y-ry,1,ry*2+1,indigo);rect(x+rx,y-ry,1,ry*2+1,indigo);
        } else rect(x+rx,y-ry,2,2,muted_ink);
    }
    if(count>0) {
        int x=view_x(city_defs[target].x),y=view_y(city_defs[target].y);
        if(zoomed && game.campaign.cities[target].king>=0) {
            const auto flag=map_flag_origin(x,y,map_field_top);
            x=flag.x+map_flag_width/2;y=flag.y+map_flag_height/2;
        }
        const int rx=zoomed ? 10 : 6,ry=zoomed ? 9 : 6;
        line(x-rx,y-ry,x+rx,y-ry,ochre); line(x-rx,y+ry,x+rx,y+ry,ochre);
        line(x-rx,y-ry,x-rx,y+ry,ochre); line(x+rx,y-ry,x+rx,y+ry,ochre);
        pixel(x,y-ry-2,text_ink);
    }
    // Keep all faction cores solid even where route/indicator pixels cross.
    if(!zoomed) for(int i=0;i<city_count;++i)
        rect(map_x(city_defs[i].x)-2,map_y(city_defs[i].y)-2,5,5,
             king_ink(game.campaign.cities[i].king,game.campaign.player));
    clip_top=0;clip_bottom=160;
    rect(0,116,240,44,paper); line(0,116,239,116,ochre);
    ui_text(4,119,"目标",muted_ink);
    ui_text(40,119,city_defs[target].name,ochre,70);
    ui_text(124,119,count<=0 ? "没有可达目标" : target==origin ? "出发城" :
        (adjacent>>target)&1 ? "相邻可达" : "经道路可达",indigo,110);
    ui_text(4,133,notice && *notice ? notice : dispatch ? "方向选城 A出征 B缩放 START返回" : "方向选城 A确认 B缩放 START返回",text_ink,230);
    rect(5,149,6,6,indigo);rect(6,150,4,4,paper);
    ui_text(15,147,"邻接可达",muted_ink,65);
    rect(85,151,2,2,muted_ink);ui_text(94,147,"间接可达",muted_ink,65);
    rect(167,149,6,6,ochre);rect(168,150,4,4,paper);
    ui_text(177,147,"目标",muted_ink,52);
}

void Render::packed_character(int x,int y,const PackedSprite& sprite) {
    if(project_field) { scaled_packed(x,y,sprite); return; }
    const int top=maximum(0,battle_view_top-y), bottom=minimum(sprite.height,battle_view_bottom-y);
    if(top>=bottom || x>=240 || x+60<=0) return;
    x &= ~3;
    auto* bytes=reinterpret_cast<uint8_t*>(screen_tiles);
    for(int py=top; py<bottom; ++py) {
        const int yy=y+py, row_offset=(yy>>3)*30*64+(yy&7)*8;
        for(int index=sprite.rows[py]; index<sprite.rows[py+1]; ++index) {
            const auto& span=sprite.spans[index];
            const int xx=x+int(span.column);
            if(unsigned(xx)>236) continue;
            auto* output=reinterpret_cast<uint32_t*>(bytes+row_offset+(xx>>3)*64+(xx&7));
            *output=span.mask==0xffffffff ? span.pixels : (span.pixels|(*output&~span.mask));
        }
    }
}

void Render::battle_terrain(int camera_x,int camera_y,int ground) {
    const int ground_shadow = ground+1;
    const int ground_light = ground+2;
    for(int y = battle_view_top+3-camera_y%5; y < battle_view_bottom; y += 5) for(int screen_x = -camera_x%8; screen_x < 240; screen_x += 8) {
        const int x = screen_x + camera_x;
        unsigned seed = unsigned(x / 8) * 0x8da6b343u ^ unsigned((y+camera_y) / 5) * 0xd8163841u;
        seed ^= seed >> 13; seed *= 0x85ebca6bu; seed ^= seed >> 16;
        const unsigned detail = seed;
        if(y<battle_view_top+2 || screen_x<0 || screen_x+7>=240) continue;
        if((detail & 7) == 0) {
            pixel(screen_x,y,ground_shadow); pixel(screen_x+1,y-1,ground_light);
            if(detail & 0x20) pixel(screen_x+2,y,ground_shadow);
        }
        if((detail & 31) == 3) {
            pixel(screen_x+4,y+1,ground_light); pixel(screen_x+5,y,ground_shadow);
            pixel(screen_x+6,y+1,ground_light);
        }
        if((detail & 63) == 9) pixel(screen_x+7,y-1,ground_shadow);
    }
}

// Two commanders do not need scarce fast RAM for their pose/feedback setup;
// the shared sprite blitter and the many-soldier loop remain in IWRAM.
__attribute__((noinline))
void Render::battle_commander(const Game& game,int side,int camera_x,int camera_y) {
    const auto& f = game.battle.sides[side];
    const bool player = game.campaign.officers[f.general].king == game.campaign.player;
    const int hit_ticks=game.battle.general_hit_ticks[side];
    const int gx = int(f.x) - camera_x + (hit_ticks>10 ? ((hit_ticks&2) ? 1 : -1) : 0);
    const int gy = int(battle_commander_y) - camera_y + battle_view_center_y;
    const auto& visual = game.battle.general_visual[side];
    const unsigned cast_elapsed=uint16_t(game.battle.tick-game.battle.cast_pose_start_tick);
    const bool casting=game.battle.effect_remaining && game.battle.effect_side==side &&
        game.battle.pending_winner<0 && f.hp>0 && cast_elapsed<36;
    const bool retreat_motion = f.order == Order::retreat && visual.motion_hold;
    const bool back_attack = !casting && !retreat_motion && f.attack_ticks &&
        f.attack_direction && (f.attack_direction>0)!=bool(visual.facing);
    // Reuse the two authored weapon poses: ready/hold, release, recovery.
    // No extra sprite, framebuffer pass or gameplay attack is scheduled.
    const int frame = casting ? (cast_elapsed<24 ? 5 : cast_elapsed<32 ? 6 : 0) :
        retreat_motion ? 1 + visual.walk_frame :
        back_attack ? 0 : f.attack_ticks ? (f.attack_ticks > 6 ? 5 : 6) :
        visual.motion_hold ? 1 + visual.walk_frame : 0;
    rect(gx-18,gy+15,36,2,ink);
    if(project_field) {
        const auto& sprite=zoom_cavalry[projection.shift-1][player ? 0 : 1][frame];
        project_field=false;
        if(back_attack) {
            const int upper=minimum(int(sprite.height),(20+(1<<projection.shift)-1)>>projection.shift);
            const int sx=projection.x(gx-28),sy=projection.y(gy-32);
            character(sx,sy,sprite.width,upper,sprite.pixels,visual.facing);
            character(sx,sy+upper,sprite.width,sprite.height-upper,sprite.pixels+upper*sprite.width,!visual.facing);
        } else character(projection.x(gx-28),projection.y(gy-32),sprite.width,sprite.height,sprite.pixels,!visual.facing);
        project_field=true;
    }
    else if(back_attack) {
        const auto* pixels=cavalry_sprites[player ? 0 : 1][0];
        character(gx-28,gy-32,56,20,pixels,visual.facing);
        character(gx-28,gy-12,56,28,pixels+56*20,!visual.facing);
    } else packed_character(gx-28,gy-32,packed_cavalry[player ? 0 : 1][frame][!visual.facing][(gx-28)&3]);
    if(back_attack) {
        // Code-native rear thrust: wind-up, extension, then recovery. The
        // horse keeps the idle silhouette; only the rider and weapon turn.
        const int d=f.attack_direction;
        const int reach=f.attack_ticks>14 ? 16 : f.attack_ticks>6 ? 35 : 25;
        const int hand_x=gx+d*3,hand_y=gy-17;
        const int tip_x=gx+d*reach,tip_y=hand_y+(f.attack_ticks>14 ? -8 : -1);
        line(gx-d*5,gy-22,hand_x,hand_y,player ? indigo : cinnabar);
        line(hand_x,hand_y+1,tip_x,tip_y+1,edge_dark);
        line(hand_x,hand_y,tip_x,tip_y,ochre);
        line(tip_x-d*5,tip_y-2,tip_x,tip_y,text_ink);
        line(tip_x-d*5,tip_y+2,tip_x,tip_y,text_ink);
    }
    if(hit_ticks>10) {
        const int hx=gx+(visual.facing ? 13 : -13), hy=gy-17;
        line(hx-5,hy,hx+5,hy,ochre);
        line(hx,hy-5,hx,hy+5,ochre);
        line(hx-3,hy-3,hx+3,hy+3,white);
        line(hx-3,hy+3,hx+3,hy-3,white);
    }
}

void Render::battle_actors(const Game& game,int camera_x,int camera_y) {
    auto draw_soldier = [&](const Soldier& s) {
        const auto& fighter = game.battle.sides[s.side];
        const int x = int(s.x)-camera_x-14;
        if(x+28<=projection.left() || x>=projection.right()) return;
        const int screen_y = int(s.y)-camera_y+battle_view_center_y;
        if(screen_y+16<=projection.top() || screen_y-16>=projection.bottom()) return;
        // Native occupancy rows are 30 world units apart. Keep the soldier
        // art inside that row so neighboring, untargetable lanes do not
        // visually overlap the commander's target lane.
        const int y = screen_y-15;
        const int team = game.campaign.officers[fighter.general].king == game.campaign.player ? 0 : 1;
        const int archetype = fighter.force == 1 ? 1 : fighter.force == 2 || fighter.force == 4 ? 2 : fighter.force >= 5 ? 3 : 0;
        const auto& visual = game.battle.soldier_visual[&s-game.battle.soldiers];
        const bool retreat_motion = fighter.order == Order::retreat && visual.motion_hold;
        const int pose = retreat_motion || visual.motion_hold ? 1 + visual.walk_frame : 0;
        rect(x+7,screen_y+15,14,1,ink);
        if(project_field) {
            const int sx=projection.x(x),sy=projection.y(y);
            project_field=false;
            // Same pixels as zoom_infantry, with flips/masks compiled offline.
            if(projection.shift==1) packed_character(sx,sy,packed_zoom_infantry[team][archetype][pose][!visual.facing][sx&3]);
            else {
                const auto& sprite=zoom_infantry[projection.shift-1][team][archetype][pose];
                character(sx,sy,sprite.width,sprite.height,sprite.pixels,!visual.facing);
            }
            project_field=true;
        }
        else packed_character(x,y,packed_infantry[team][archetype][pose][!visual.facing][x&3]);
    };
    for(const auto& s : game.battle.soldiers) if(s.hp > 0 && s.y < battle_commander_y) draw_soldier(s);
    for(int side=0;side<2;++side) battle_commander(game,side,camera_x,camera_y);
    for(const auto& s : game.battle.soldiers) if(s.hp > 0 && s.y >= battle_commander_y) draw_soldier(s);
}

void Render::combat(Game& game, int skill_slot, const char* notice, bool fit_all) {
    project_field=false;
    map_ui_active=false;
    if(!combat_active) upload_full=true;
    int stage_ticks = bn::core::current_cpu_ticks();
    auto mark_stage = [&](int slot) {
        const int now = bn::core::current_cpu_ticks();
        sg_probe[slot] = maximum(uint32_t(sg_probe[slot]),uint32_t(now-stage_ticks));
        stage_ticks = now;
    };
    int attacker_front = battlefield_width - 1, defender_front = 0;
    bool has_attacker_front = false, has_defender_front = false;
    for(const auto& s : game.battle.soldiers) if(!game.battle.camera_manual && s.hp > 0) {
        if(s.side == 0 && (!has_attacker_front || s.x < attacker_front)) { attacker_front = s.x; has_attacker_front = true; }
        if(s.side == 1 && (!has_defender_front || s.x > defender_front)) { defender_front = s.x; has_defender_front = true; }
    }
    const int controlled = game.campaign.officers[game.battle.sides[0].general].king == game.campaign.player ? 0 : 1;
    const auto& player_fighter = game.battle.sides[controlled];
    // Follow the player's group. Centering the midpoint between both lines
    // keeps the camera fixed even as the battle moves across the field; using
    // the player's commander/front midpoint keeps both visible during advance.
    const int player_front = controlled == 0 ? (has_attacker_front ? attacker_front : player_fighter.x) :
        (has_defender_front ? defender_front : player_fighter.x);
    int group_span = player_front-int(player_fighter.x);
    if(group_span<0) group_span=-group_span;
    int focus_x = group_span>battle_view_width-60 ? int(player_fighter.x) : (player_front+int(player_fighter.x))/2;
    const auto subjects=visual::effect_camera_subjects(game.battle);
    if(player_fighter.order == Order::retreat || player_fighter.order == Order::general || player_fighter.soldiers == 0)
        focus_x = player_fighter.x;
    // Keep an automatically followed offensive skill in view as it travels.
    if(!game.battle.camera_manual && player_fighter.order!=Order::retreat) {
        const int direction=game.battle.sides[1-controlled].x>player_fighter.x ? 1 : -1;
        int best_progress=-1;
        for(const auto& projectile : game.battle.projectiles) {
            if(!visual::camera_tracks(projectile) || projectile.target_side!=1-controlled) continue;
            const int progress=(int(projectile.x)-player_fighter.x)*direction;
            const int target_limit=(int(game.battle.sides[1-controlled].x)-player_fighter.x)*direction+battle_grid_column_width;
            const int row_offset=int(projectile.y)-battle_commander_y;
            if(progress>=0 && progress<=target_limit && progress>best_progress && row_offset>=-45 && row_offset<=45) {
                best_progress=progress; focus_x=projectile.x;
            }
        }
        if(best_progress<0 && game.battle.effect_side==controlled &&
           game.battle.effect_kind!=1 && game.battle.effect_kind!=2 && game.battle.effect_ticks &&
           !original::magic_uses_collision_projectile(game.battle.effect_script) &&
           !original::sword_wave_projectile_count(game.battle.effect_script))
            focus_x=game.battle.sides[1-controlled].x;
    }
    // Delayed lightning children outlive the root setup timer. Keep their
    // landing region visible until their own lifetimes end, including AI casts.
    if(!game.battle.camera_manual && subjects.lightning_count)
        focus_x=subjects.lightning_sum/subjects.lightning_count;
    // Enemy-centered sweep stays in view after the general hit is consumed.
    if(subjects.dragon) focus_x=subjects.dragon->target_offset_x;
    if(game.battle.pending_winner == 2) focus_x = (game.battle.sides[0].x + game.battle.sides[1].x) / 2;
    else if(game.battle.pending_winner >= 0) focus_x = game.battle.sides[1-game.battle.pending_winner].x;
    if(!game.battle.camera_manual)
        game.battle.camera_x = int16_t(clamp(focus_x - battle_view_width / 2,0,battle_camera_max_x(game)));
    if(!game.battle.camera_y_manual) game.battle.camera_y = int16_t(battle_commander_y);
    int camera_x = game.battle.camera_x;
    int camera_y = game.battle.camera_y;
    if(!fit_all && !game.battle.camera_manual && !game.battle.camera_y_manual) {
        if(const auto* fire=subjects.firewheel) {
            camera_x=clamp(int(fire->target_offset_x)-120,0,battle_camera_max_x(game));
            camera_y=fire->height;
        }
    }
    if(!fit_all && !game.battle.camera_manual && !game.battle.camera_y_manual) {
        if(const auto* cyclone=subjects.cyclone) {
            camera_x=clamp(int(cyclone->x)-120,0,battle_camera_max_x(game));
            camera_y=cyclone->y-24;
        }
    }
    if(!game.battle.camera_manual && game.battle.effect_remaining && game.battle.effect_script==2650) {
        int seal_left=battlefield_width, seal_right=0;
        bool seal_active=false;
        for(const auto& projectile : game.battle.projectiles) {
            if(!visual::effect_visible(projectile) || projectile.magic!=12 ||
               projectile.source_script!=2650) continue;
            seal_active=true;
            seal_left=minimum(seal_left,int(projectile.x));
            seal_right=maximum(seal_right,int(projectile.x));
        }
        if(seal_active) {
            // Glyph objects are drawn from their raw top-left origins. Their
            // 32px extent shifts the center of the field 16px right of the
            // marker-coordinate midpoint.
            camera_x=clamp((seal_left+seal_right)/2+16-battle_view_width/2,
                           0,battle_camera_max_x(game));
            game.battle.camera_x=int16_t(camera_x);
        }
    }
    bool chariot_visual_active=false;
    for(const auto& projectile : game.battle.projectiles) if(projectile.active &&
        projectile.magic==20 && (projectile.source_script==2627 || projectile.source_script==2628))
        chariot_visual_active=true;
    const bool chariot_cast=game.battle.pending_winner<0 &&
        (game.battle.effect_script==2627 || game.battle.effect_script==2628) &&
        (game.battle.effect_remaining || chariot_visual_active);
    int chariot_elapsed=0;
    int chariot_focus_x=0;
    if(chariot_cast) {
        chariot_elapsed=game.battle.effect_remaining ?
            original::magic_busy_duration(game.battle.effect_script)-game.battle.effect_remaining :
            original::magic_busy_duration(game.battle.effect_script);
        chariot_focus_x=game.battle.magic_field_center_x;
        if(!game.battle.camera_manual && chariot_elapsed>=50) {
            // MAGIC01 4000 passes local5+320 to native256 (except its
            // explicit zero-coordinate branch, which subtracts320). For an
            // empty roster local5 already includes its side-specific +/-320
            // fallback; the native256 offset is still an additional +320.
            const int native_camera_offset=320*battle_grid_column_width/96;
            const int camera_center=chariot_focus_x ?
                chariot_focus_x+native_camera_offset : chariot_focus_x-native_camera_offset;
            // MAGIC01 4000 calls native256 once at update50; the camera jumps
            // to this selected viewport before the 15-update child setup.
            camera_x=clamp(camera_center-battle_view_width/2,0,battle_camera_max_x(game));
        }
    }
    projection.shift=0;
    if(fit_all) {
        int left=minimum(int(game.battle.sides[0].x),int(game.battle.sides[1].x))-40;
        int right=maximum(int(game.battle.sides[0].x),int(game.battle.sides[1].x))+40;
        int top=battle_commander_y-40, bottom=battle_commander_y+28;
        for(const auto& soldier : game.battle.soldiers) if(soldier.hp>0) {
            left=minimum(left,int(soldier.x)-20); right=maximum(right,int(soldier.x)+20);
            top=minimum(top,int(soldier.y)-24); bottom=maximum(bottom,int(soldier.y)+24);
        }
        // Include the complete travelling effect, not just its collision point.
        for(const auto& p : game.battle.projectiles) if(visual::effect_frames_camera(p)) {
            const int px=p.x,py=visual::effect_frame_y(p);
            int x=px-48,y=py-48,w=96,h=96;
            // Use sprite canvases/anchors, not a 192px box for every tiny
            // arrow or clone. All coordinates are still unscaled world units.
            if(p.magic==0 || p.magic==2) { x=px-7;y=py-7-(p.magic==2 ? 9 : 0);w=h=16; }
            else if(p.magic==1 && ((p.source_script>=2645 && p.source_script<=2647) || p.kind==1)) {
                x=px-44;y=visual::grounded_top(py,magic_clone_slash_height);w=88;h=magic_clone_slash_height;
            } else if(p.magic==1 && p.source_script>=2642 && p.source_script<=2644) {
                x=px-84;y=py-48;w=168;h=48; // Raised body and segmented tail.
            } else if(p.magic==1 && p.kind>=2) {
                x=visual::centered_left(px,magic_fire_hit_width);y=visual::grounded_top(py,magic_fire_hit_height);w=magic_fire_hit_width;h=magic_fire_hit_height;
            } else if(p.magic==1) { x=px-40;y=py-40;w=80;h=64; }
            else if(p.magic==3) {
                const int frame=clamp((original::minefield_visual_lifetime-int(p.remaining))/2,0,6);
                x=px-magic_m005_mine_x_offset[frame];y=py-magic_m005_mine_y_offset[frame];w=magic_m005_mine_width;h=magic_m005_mine_height;
            } else if(p.magic==4) {
                const int frame=p.phase>=33 ? 0 : ((int(p.phase)-1)/2)&7;
                x=px-magic_sword_sweep_x_offset[frame];y=py-magic_sword_sweep_y_offset[frame];w=h=magic_sword_sweep_size;
            } else if(p.magic==5) {
                x=visual::centered_left(px,48);y=visual::grounded_top(py,24)-48;w=48;h=72;
            } else if(p.magic==6) {
                x=visual::centered_left(px,magic_strike_width);y=visual::grounded_top(py,magic_strike_height);w=magic_strike_width;h=magic_strike_height;
            } else if(p.magic==7 || p.magic==20) {
                static constexpr uint8_t frames[14]={0,1,1,2,3,2,1,0,1,2,2,3,2,1};
                const int age=p.magic==20 ? p.height : maximum(0,original::heaven_and_earth_beam_lifetime(p.source_script)-p.height);
                const int frame=frames[(age/4)%14];
                x=px-magic_chariot_x_offset[frame];y=py-magic_chariot_y_offset[frame];w=magic_chariot_width;h=magic_chariot_height;
            } else if(p.magic==8 || p.magic==9) {
                x=visual::centered_left(px,magic_lightning_width);y=visual::grounded_top(py,magic_lightning_height);w=magic_lightning_width;h=magic_lightning_height;
            } else if(p.magic==11) { x=px;y=py;w=h=magic_m007_stone_size; }
            else if(p.magic==12) {
                const int frame=p.kind&7;
                x=px-magic_eight_gates_x_offset[frame];y=py-magic_eight_gates_y_offset[frame];w=h=magic_eight_gates_size;
            } else if(p.magic==13 && p.source_script!=2665) {
                x=visual::centered_left(px,magic_m020_field_size);y=visual::grounded_top(py,magic_m020_field_size);w=h=magic_m020_field_size;
            } else if(p.magic==13) {
                const int frame=p.kind&3;
                x=px-magic_wave_sprite_x_offset[frame];y=py-magic_wave_sprite_y_offset[frame];w=h=magic_wave_sprite_size;
            } else if(p.magic==15) {
                const int age=p.height;
                const int frame=age<38 ? 0 : age<41 ? 1 : age<42 ? 2 : age<43 ? 3 : age<44 ? 4 : age<45 ? 5 : age<46 ? 6 : 7;
                x=px-magic_m007_catapult_x_offset[frame];y=py-magic_m007_catapult_y_offset[frame];w=h=magic_m007_catapult_size;
            } else if(p.magic==25) {
                const int frame=clamp(int(p.kind),0,2);
                x=px-yellow_dragon_fire_x_offset[frame];y=py-yellow_dragon_fire_y_offset[frame];w=yellow_dragon_fire_width[frame];h=yellow_dragon_fire_height[frame];
            }
            left=minimum(left,x);right=maximum(right,x+w);
            top=minimum(top,y);bottom=maximum(bottom,y+h);
        }
        projection.shift=BattleProjection::fit_shift(right-left+16,bottom-top+16);
        camera_x=(left+right)/2-120; camera_y=(top+bottom)/2;
    }
    // Frame this skill's footprint, not every distant soldier: a field-wide
    // fit can shrink the dragon to an unreadable dot. Respect manual framing.
    if(!fit_all && !game.battle.camera_manual && !game.battle.camera_y_manual)
        if(const auto* dragon=subjects.dragon) {
            const auto& p=*dragon;
            projection.shift=BattleProjection::fit_shift(380,60*(int(p.scatter)-1)+64);
            camera_x=p.target_offset_x-120;
            camera_y=battle_commander_y-24;
        }
    // Show real rear-entry soldiers, not an empty patch by the commander.
    // Frame just the new squad so faraway units do not shrink it needlessly.
    if(!fit_all && !game.battle.camera_manual && !game.battle.camera_y_manual)
        if(const auto* ambush=subjects.ambush) {
            const auto& p=*ambush;
            projection.shift=BattleProjection::fit_shift(p.target_offset_x,p.height);
            camera_x=p.x-120;camera_y=p.y;
        }
    sg_probe[135]=1u<<projection.shift;
    const int field_left=projection.left(), field_right=projection.right();
    const int field_top=projection.top(), field_bottom=projection.bottom();
    int terrain_x, terrain_y;
    if(game.battle.city >= 0) {
        terrain_x = city_defs[game.battle.city].x;
        terrain_y = city_defs[game.battle.city].y;
    } else if(game.battle.army >= 0) {
        const auto& army = game.campaign.armies[game.battle.army];
        terrain_x = (int(city_defs[army.origin].x) + int(city_defs[army.target].x)) / 2;
        terrain_y = (int(city_defs[army.origin].y) + int(city_defs[army.target].y)) / 2;
    } else {
        terrain_x = terrain_y = 0;
    }
    static int cached_terrain_x=-1, cached_terrain_y=-1, ground=112;
    if(cached_terrain_x!=terrain_x || cached_terrain_y!=terrain_y) {
        ground = map_ground_ink(terrain_x,terrain_y)==tan ? 115 : 112;
        cached_terrain_x=terrain_x; cached_terrain_y=terrain_y;
    }
    const int g = game.battle.sides[controlled].general;
    const int available_skills = game.skill_count(g);
    const auto* skill = available_skills ? skill_by_id(game.learned_skill(g,skill_slot%available_skills)) : nullptr;
    static int cached_generals[2]={-1,-1}, cached_ground=-1, cached_skill=-1, cached_controlled=-1;
    static int cached_camera_x=-1, cached_camera_y=-1, cached_shift=-1, cached_fit=-1;
    const int skill_id=skill ? game.learned_skill(g,skill_slot%available_skills) : -1;
    bool backdrop_dirty=false;
    if(cached_generals[0]!=game.battle.sides[0].general || cached_generals[1]!=game.battle.sides[1].general ||
       cached_ground!=ground || cached_skill!=skill_id || cached_controlled!=controlled) {
        clear(background);
        battle_frame();
        rect(0,0,240,battle_view_top,paper);
        rect(0,battle_view_top,240,battle_view_height,ground);
        rect(0,battle_view_bottom,240,160-battle_view_bottom,surface);
        battle_general_headers(game,controlled);
        text(6,146,skill ? skill->name : "--",text_ink);
        text(80,146,"耗",indigo); number(95,146,skill ? skill->mp : 0,indigo);
        backdrop_dirty=true;
        cached_generals[0]=game.battle.sides[0].general;
        cached_generals[1]=game.battle.sides[1].general;
        cached_ground=ground; cached_skill=skill_id; cached_controlled=controlled;
    } else {
        const bool camera_changed=cached_camera_x!=camera_x || cached_camera_y!=camera_y || cached_shift!=projection.shift || cached_fit!=int(fit_all);
        if(combat_active) {
            // HUD/footer pixels persist between frames. Only actors and
            // effects need their ground restored; camera changes repaint it
            // below. Avoid copying unchanged UI bands out of EWRAM each draw.
            if(!camera_changed) bn::memory::copy(battle_backdrop[6*60],12*60,screen_tiles[6*60]);
        } else if(camera_changed) {
            bn::memory::copy(battle_backdrop[0],6*60,screen_tiles[0]);
            bn::memory::copy(battle_backdrop[18*60],2*60,screen_tiles[18*60]);
        } else bn::memory::copy(battle_backdrop[0],1202,screen_tiles[0]);
    }
    static int cached_hud[2][5]={{-1,-1,-1,-1,-1},{-1,-1,-1,-1,-1}};
    bool changed[2][5];
    bool hud_dirty=backdrop_dirty;
    for(int side=0; side<2; ++side) {
        const auto& fighter=game.battle.sides[side];
        const int values[5]={fighter.hp,fighter.mp,fighter.soldiers,100*int(fighter.charge)/120000,game.battle.general_hit_ticks[side]};
        for(int item=0; item<5; ++item) {
            changed[side][item]=backdrop_dirty || cached_hud[side][item]!=values[item];
            if(changed[side][item]) hud_dirty=true;
            cached_hud[side][item]=values[item];
        }
    }
    if(hud_dirty) for(int side=0; side<2; ++side) {
        const auto& fighter=game.battle.sides[side];
        const int x=side==controlled ? 8 : 130;
        const bool hit=game.battle.general_hit_ticks[side]>0;
        if(changed[side][0] || changed[side][4]) {
        rect(x+13,24,18,12,paper);
        number(x+13,24,fighter.hp,hit ? cinnabar : text_ink);
        rect(x+61,0,24,12,paper);
        if(hit) { text(x+61,0,"-",cinnabar); number(x+67,0,game.battle.general_hit_damage[side],cinnabar); }
        rect(x,36,100,4,pale_paper);
        rect(x,36,clamp(100*int(fighter.hp)/maximum(1,game.max_hp(fighter.general)),0,100),4,
             hit && (game.battle.general_hit_ticks[side]&2) ? ochre : cinnabar);
        }
        if(changed[side][1]) {
        rect(x+51,24,18,12,paper); number(x+51,24,fighter.mp,indigo);
        rect(x,40,100,4,pale_paper);
        rect(x,40,clamp(100*int(fighter.mp)/maximum(1,game.max_mp(fighter.general)),0,100),4,indigo);
        }
        if(changed[side][2]) {
            rect(x+85,24,18,12,paper); number(x+85,24,fighter.soldiers,text_ink);
        }
        if(changed[side][3]) {
            rect(x,44,100,4,pale_paper); rect(x,44,cached_hud[side][3],4,ochre);
        }
    }
    upload_hud |= hud_dirty;
    mark_stage(115);
    const bool ground_dirty=backdrop_dirty || cached_camera_x!=camera_x || cached_camera_y!=camera_y || cached_shift!=projection.shift || cached_fit!=int(fit_all);
    if(ground_dirty) {
    rect(0,battle_view_top,240,battle_view_height,ground);
    if(!fit_all) battle_terrain(camera_x,camera_y,ground);
    cached_camera_x=camera_x; cached_camera_y=camera_y; cached_shift=projection.shift; cached_fit=int(fit_all);
    }
    if(backdrop_dirty) bn::memory::copy(screen_tiles[0],1202,battle_backdrop[0]);
    else {
        if(ground_dirty) bn::memory::copy(screen_tiles[6*60],12*60,battle_backdrop[6*60]);
        if(hud_dirty) bn::memory::copy(screen_tiles[0],6*60,battle_backdrop[0]);
    }
    mark_stage(116);
    clip_top=battle_view_top; clip_bottom=battle_view_bottom;
    project_field=projection.shift!=0;
    const Projectile* yellow_cast=subjects.yellow;
    if(yellow_cast && game.battle.pending_winner<0) {
        const int elapsed=uint16_t(game.battle.tick-uint16_t(yellow_cast->dx));
        const int caster_side=int(yellow_cast->target_side);
        const auto& caster=game.battle.sides[caster_side];
        const int caster_x=int(caster.x)-camera_x;
        const int caster_y=battle_commander_y-camera_y+battle_view_center_y;
        const int dragon_start=20+clamp(int(yellow_cast->scatter),10,50);
        const uint32_t cast_seed=uint32_t(game.battle.tick)-uint32_t(elapsed);
        // MAGIC01 3600 waits20 before starting child3601; that child draws
        // RNG(10,50), creates the persistent M006dragon, then runs130 updates.
        if(elapsed>=dragon_start && elapsed<dragon_start+130) {
            const int dragon_x_offset=sg::yellow_dragon_x_offset;
            character_white_light(caster_x-dragon_x_offset,
                      caster_y-sg::yellow_dragon_y_offset,
                      sg::yellow_dragon_width,sg::yellow_dragon_height,
                      sg::yellow_dragon_sprite,false);
        }
        if(elapsed>=20 && elapsed<230) {
            // GLOBAL3600 runs195 one-update loops. In mode2 every sampled
            // one-or-two A-orb spawn is mirrored at +/-48 native x, and a
            // separate one-in-four roll adds a C orb. Things.ini keeps each
            // orb's two #8 frames for16 updates, so retain the last16 births.
            const int loop_tick=elapsed-20;
            const int first_birth=maximum(0,loop_tick-15);
            const int last_birth=minimum(194,loop_tick);
            for(int born=first_birth;born<=last_birth;++born) {
                // Decorative orb births only: keep the same path/lifetime,
                // but avoid dozens of overlapping additive copies on GBA.
                if(born%3) continue;
                const uint32_t base=uint32_t(born*1103515245u+0x3600u) ^
                    (cast_seed*0x9e3779b9u);
                const int count=1+int((base>>5)&1u);
                for(int i=0;i<count;++i) {
                    const uint32_t seed=base+uint32_t(i*12345u+0x3602u);
                    const int native_dx=int((seed>>8)%97)-48;
                    const int native_dy=int((seed>>16)%33)-16;
                    const int center_x=caster_x+native_dx*battle_grid_column_width/96;
                    const int y=caster_y+native_dy*battle_grid_row_height/72;
                    // MAGIC01 3600 rolls sequence3602/3603 independently
                    // for each mirrored object. Keep the GBA visual seed's
                    // two choices independent instead of mirroring one frame.
                    const int left_sprite=int((seed>>24)&1u);
                    const int right_sprite=int((seed>>28)&1u);
                    for(int side=-1;side<=1;side+=2) {
                        const int sprite=side<0 ? left_sprite : right_sprite;
                        const int x=center_x+side*48*battle_grid_column_width/96;
                        const int draw_x=x-sg::yellow_dragon_orb_x_offset[sprite];
                        const int draw_y=y-sg::yellow_dragon_orb_y_offset[sprite];
                        if(draw_x+sg::yellow_dragon_orb_size>field_left &&
                           draw_x<field_right && draw_y+sg::yellow_dragon_orb_size>field_top &&
                           draw_y<field_bottom)
                            character_white_light(draw_x,draw_y,
                                      sg::yellow_dragon_orb_size,
                                      sg::yellow_dragon_orb_size,
                                      sg::yellow_dragon_orbs[sprite],false);
                    }
                }
                // MAGIC01 3600 tests RNG(0,3)==0: four possible results,
                // so the source C orb branch is one in four, not one in three.
                if(((base>>24)&3u)==0u) {
                    // Keep the C branch's RNG calls independent and in the
                    // source order: placement, constructor angle, sequence,
                    // then native23's planar and vertical speeds. The root
                    // and sibling scripts still share an unreplayed stream.
                    uint32_t orb_random=base^0x3605a17u;
                    auto orb_roll=[&](uint32_t limit) {
                        orb_random=orb_random*1664525u+1013904223u;
                        return int((orb_random>>8)%limit);
                    };
                    const int native_dx=orb_roll(97)-48;
                    const int native_dy=orb_roll(33)-16;
                    const int age=elapsed-(20+born);
                    const int angle=orb_roll(256);
                    const int sprite=3+orb_roll(2); // sequence3605/3606
                    const int speed=10+orb_roll(7);
                    const int rise=10+orb_roll(7);
                    // native23's second RNG(10,16) value is the initial
                    // vertical velocity in native height units. These
                    // sequences do not set OF_NOGRAVITY. FUN_0041d20e moves
                    // first, then subtracts 2*0xa000 (1.25 native units) from
                    // positive vertical velocity. Keep its Q16 accumulation
                    // exactly in eighth-native-unit steps before rendering.
                    const int x_velocity=magic_planar_particle_velocity(speed,angle,0);
                    const int y_velocity=magic_planar_particle_velocity(speed,angle,1);
                    const int x=caster_x+native_dx*battle_grid_column_width/96+
                        (x_velocity*age)/8;
                    const int y=caster_y+native_dy*battle_grid_row_height/72+
                        (y_velocity*age)/8;
                    const int height=maximum(0,(8*age*rise-5*age*(age-1))/8);
                    const int draw_x=x-sg::yellow_dragon_orb_x_offset[sprite];
                    const int draw_y=y-height-sg::yellow_dragon_orb_y_offset[sprite];
                    if(draw_x+sg::yellow_dragon_orb_size>field_left && draw_x<field_right &&
                       draw_y+sg::yellow_dragon_orb_size>field_top &&
                       draw_y<field_bottom)
                        character_white_light(draw_x,draw_y,
                                  sg::yellow_dragon_orb_size,
                                  sg::yellow_dragon_orb_size,
                                  sg::yellow_dragon_orbs[sprite],false);
                }
            }
        }
    }
    battle_actors(game,camera_x,camera_y);
    if(game.battle.magic_field_state==1 &&
       original::reinforcement_amount(game.battle.effect_script) &&
       game.battle.heal_visual_remaining && game.battle.pending_winner<0) {
        const int elapsed=68-int(game.battle.heal_visual_remaining);
        if(elapsed>=20) {
            const int age=elapsed-20;
            const auto& caster=game.battle.sides[game.battle.effect_side];
            // GLOBAL6400 creates sequence6400 at z+256. Its callback creates
            // sequence6401 at another z+16, gives it native24(0,45,5),
            // gravity, and a native38(16,3) 48-update lifetime. Use 1/8
            // native-unit motion so its fall remains smooth on the GBA grid.
            const int x=int(caster.x)-camera_x+
                magic_particle_velocity(5,0,45,0)*age/8;
            const int vertical_speed_q16=magic_native_vertical_velocity_q16(5,45);
            const int z_q16=272*65536+age*vertical_speed_q16-
                            10752*age*(age-1);
            const int z=maximum(0,z_q16/65536);
            const int y=battle_commander_y-camera_y+battle_view_center_y-
                z*battle_grid_row_height/72;
            const int frame=(age/2)%6;
            if(x+sg::magic_reinforce_fail_size/2>=field_left &&
               x-sg::magic_reinforce_fail_size/2<field_right &&
               y+sg::magic_reinforce_fail_size/2>field_top &&
               y-sg::magic_reinforce_fail_size/2<field_bottom)
                character(x-sg::magic_reinforce_fail_size/2,
                          y-sg::magic_reinforce_fail_size/2,
                          sg::magic_reinforce_fail_size,
                          sg::magic_reinforce_fail_size,
                          sg::magic_reinforce_fail_sprites[frame],
                          false);
        }
    }
    if((game.battle.effect_script==2666 || game.battle.effect_script==2667) &&
       game.battle.heal_visual_remaining && game.battle.pending_winner<0) {
        const int elapsed=126-game.battle.heal_visual_remaining;
        // GLOBAL6300 emits three selected M023 objects on each of 60 updates.
        // native38(16,2) advances their 16-step animation every two updates,
        // so each object remains alive for 32 battle updates.
        if(elapsed>=35 && elapsed<126) {
            const auto& caster=game.battle.sides[game.battle.effect_side];
            const int center_x=int(caster.x)-camera_x;
            const int center_y=battle_commander_y-camera_y+battle_view_center_y;
            const int loop_tick=elapsed-35;
            const int first_tick=loop_tick>31 ? loop_tick-31 : 0;
            const int last_tick=loop_tick<59 ? loop_tick : 59;
            const unsigned sprite_index=game.battle.effect_script==2667 ? 1u : 0u;
            for(int born=first_tick;born<=last_tick;++born) {
                if(born%4) continue; // Healing amount/timing are not sampled.
                // native21 uses each local6 angle, shrinking local9 radius,
                // and local7 height. Its first two coordinates are a polar
                // offset from the caster, not a linear x displacement.
                // GLOBAL6300 starts local9 at32*65536. Each iteration adds
                // 64 to local8, adds local8 to local3, then subtracts local3
                // from local9. Summing 64+128+...+64*born gives this
                // quadratic decrement; the previous cubic expression pulled
                // the particles into the center much too quickly.
                const int radius_q16=maximum(0,2097152-32*born*(born+1));
                const int radius=radius_q16/65536;
                for(int particle=0;particle<3;++particle) {
                    const int angle=born*18+(particle*256)/3;
                    const int native_x=magic_native_polar_offset(radius,angle+64);
                    const int native_y=magic_native_polar_offset(radius,angle);
                    const int native_z=born*2+1;
                    const int x=center_x+native_x*battle_grid_column_width/96;
                    const int y=center_y+native_y*battle_grid_row_height/72-
                                native_z*battle_grid_row_height/72;
                    const int draw_x=x-sg::magic_healing_x_offset[sprite_index];
                    const int draw_y=y-sg::magic_healing_y_offset[sprite_index];
                    if(draw_x+sg::magic_healing_size>field_left && draw_x<field_right &&
                       draw_y+sg::magic_healing_size>field_top &&
                       draw_y<field_bottom)
                        character_white_light(draw_x,draw_y,
                                  sg::magic_healing_size,sg::magic_healing_size,
                                  sg::magic_healing_sprites[sprite_index],false);
                }
            }
        }
    }
    mark_stage(117);
    clip_top=battle_view_top; clip_bottom=battle_view_bottom;
    for(const auto& emitter : game.battle.m014_spark_emitters) if(emitter.active) {
        const int age=uint16_t(game.battle.tick-emitter.start_tick);
        if(age>=64) continue;
        const int base_x=int(emitter.x)-camera_x;
        const int base_y=int(emitter.y)-camera_y+battle_view_center_y;
        for(int i=0;i<20;i+=4) {
            const int variant=emitter.variants[i];
            const int azimuth=emitter.azimuths[i];
            const int elevation=emitter.elevations[i];
            const int dx=magic_particle_velocity(4,azimuth,elevation,0)*age/8;
            const int dy=magic_particle_velocity(4,azimuth,elevation,1)*age/8;
            const int z_velocity=magic_native_vertical_velocity_q16(4,elevation);
            const int z_q16=age*z_velocity-10752*age*(age-1);
            const int z=maximum(0,(z_q16*30)/(65536*72));
            const int draw_x=base_x+dx-sg::magic_m014_spark_x_offset[variant];
            const int draw_y=base_y+dy-z-sg::magic_m014_spark_y_offset[variant];
            if(draw_x+sg::magic_m014_spark_size>field_left && draw_x<field_right &&
               draw_y+sg::magic_m014_spark_size>field_top &&
               draw_y<field_bottom)
                character_white_light(draw_x,draw_y,sg::magic_m014_spark_size,
                          sg::magic_m014_spark_size,
                          sg::magic_m014_spark_sprites[variant],false);
        }
    }
    for(const auto& p : game.battle.projectiles) if(p.active) {
        if(p.magic && !visual::effect_visible(p)) continue;
        const int screen_y = int(p.y)-camera_y+battle_view_center_y;
        // M020 flying chips are decoration, not the colliding stone/sword
        // objects. Sample by stable pool slot so fragments never flicker.
        if(p.magic==14 && (&p-game.battle.projectiles)%4) continue;
        if(p.magic==24 || p.magic==34 || p.magic==35) continue; // invisible scheduler tokens
        if(p.magic==33) {
            const int x=int(p.x)-camera_x;
            if(x+sg::magic_m007_break_size>field_left && x<field_right &&
               screen_y+sg::magic_m007_break_size>field_top &&
               screen_y<field_bottom)
                character(x,screen_y,sg::magic_m007_break_size,
                          sg::magic_m007_break_size,
                          sg::magic_m007_break_sprites[p.kind],false);
            continue;
        }
        if(p.magic==32) {
            const int age=int(p.height);
            const int x=int(p.x)-camera_x;
            const int offset_x=int(p.dx)*age/8;
            const int offset_y=int(p.target_offset_x)*age/8;
            const int z_eighth=magic_native_gravity_height_eighth(
                int8_t(p.attacker_rear),age);
            const int z=maximum(0,z_eighth/8);
            const int draw_x=x+offset_x;
            const int draw_y=screen_y+offset_y-z;
            if(draw_x+sg::magic_m007_break_size>field_left && draw_x<field_right &&
               draw_y+sg::magic_m007_break_size>field_top &&
               draw_y<field_bottom)
                character(draw_x,draw_y,sg::magic_m007_break_size,
                          sg::magic_m007_break_size,
                          sg::magic_m007_break_sprites[p.kind],false);
            continue;
        }
        if(p.magic==25 && p.phase==1) {
            const int variant=clamp(int(p.kind),0,2);
            const int width=sg::yellow_dragon_fire_width[variant];
            const int height=sg::yellow_dragon_fire_height[variant];
            const int x=int(p.x)-camera_x;
            const int plume_y=screen_y-int(p.height);
            const int draw_x=x-sg::yellow_dragon_fire_x_offset[variant];
            const int draw_y=plume_y-sg::yellow_dragon_fire_y_offset[variant];
            const uint8_t* sprites[3]={sg::yellow_dragon_fire_0,
                                       sg::yellow_dragon_fire_1,
                                       sg::yellow_dragon_fire_2};
            if(draw_x+width>field_left && draw_x<field_right && draw_y+height>field_top &&
               draw_y<field_bottom)
                character_white_light(draw_x,draw_y,width,height,sprites[variant],false);
            continue;
        }
        if(p.magic==27 && p.phase==1) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y;
            const int frame=(int(p.height)/2)&1;
            const int x_offset=sg::magic_m011_row_x_offset[frame];
            const int draw_x=x-x_offset;
            const int draw_y=y-sg::magic_m011_row_y_offset[frame];
            if(draw_x+sg::magic_m011_row_width>=field_left &&
               draw_x<field_right && draw_y+sg::magic_m011_row_height>field_top &&
               draw_y<field_bottom)
                          character_white_light(draw_x,draw_y,
                          sg::magic_m011_row_width,sg::magic_m011_row_height,
                          sg::magic_m011_row_sprites[frame],false);
            continue;
        }
        if(p.magic==22) {
            const int x=int(p.x)-camera_x;
            // The model retains the native callback's +32y and random jitter.
            // Match the traveling M012 art's torso anchor for presentation.
            const int burst_y=screen_y-12-32*battle_grid_row_height/72;
            const int frame=clamp(int(p.height)/4,0,5);
            if(x+sg::magic_m012_burst_width/2>=field_left &&
               x-sg::magic_m012_burst_width/2<field_right &&
               burst_y+sg::magic_m012_burst_height/2>field_top &&
               burst_y-sg::magic_m012_burst_height/2<field_bottom)
                character_white_light(x-sg::magic_m012_burst_width/2,
                          burst_y-sg::magic_m012_burst_height/2,
                          sg::magic_m012_burst_width,sg::magic_m012_burst_height,
                          sg::magic_m012_burst_sprites[p.kind ? 1 : 0][frame],false);
            continue;
        }
        if(p.magic==23 && p.phase==1) {
            const int x=int(p.x)-camera_x;
            const int flame_y=screen_y-int(p.height);
            const int draw_x=x-sg::yellow_dragon_flame_x_offset;
            const int draw_y=flame_y-sg::yellow_dragon_flame_y_offset;
            if(draw_x+sg::yellow_dragon_flame_width>field_left && draw_x<field_right &&
               draw_y+sg::yellow_dragon_flame_height>field_top &&
               draw_y<field_bottom)
                character_white_light(draw_x,draw_y,
                          sg::yellow_dragon_flame_width,
                          sg::yellow_dragon_flame_height,
                          sg::yellow_dragon_flame,false);
            continue;
        }
        if(p.magic==21 && p.phase==1 && p.source_script==2663) {
            const int x=int(p.x)-camera_x;
            const int particle_y=screen_y-int(p.height);
            if(x+sg::magic_chariot_hit_width/2>=field_left &&
               x-sg::magic_chariot_hit_width/2<field_right &&
               particle_y+sg::magic_chariot_hit_height/2>field_top &&
               particle_y-sg::magic_chariot_hit_height/2<field_bottom)
                character_white_light(x-sg::magic_chariot_hit_width/2,
                          particle_y-sg::magic_chariot_hit_height/2,
                          sg::magic_chariot_hit_width,sg::magic_chariot_hit_height,
                          sg::magic_chariot_hit_sprite,false);
            continue;
        }
        if(p.magic==20 && p.kind==1 && p.phase==1 &&
           (p.source_script==2627 || p.source_script==2628)) {
            // Each GLOBAL2508 child owns one Things sequence4000 cart.
            // M010's 14 slots advance every four updates; sound-only slots
            // retain the previous image.
            static constexpr uint8_t frame_sequence[14]={0,1,1,2,3,2,1,0,1,2,2,3,2,1};
            const int age=int(p.height);
            const int frame=frame_sequence[(maximum(0,age)/4)%14];
            const int x=int(p.x)-camera_x;
            const int x_offset=sg::magic_chariot_x_offset[frame];
            const int draw_x=x-x_offset;
            const int draw_y=screen_y-sg::magic_chariot_y_offset[frame];
            if(draw_x+sg::magic_chariot_width>field_left && draw_x<field_right &&
               draw_y+sg::magic_chariot_height>field_top &&
               draw_y<field_bottom)
                character(draw_x,draw_y,
                          sg::magic_chariot_width,sg::magic_chariot_height,
                          sg::magic_chariot_sprites[frame],false);
        } else if(p.magic==17 && p.remaining && p.target_side>=0 && p.target_side<2) {
            int x=0, base_y=0;
            if(p.owner_index==0xff) {
                x=int(game.battle.sides[int(p.target_side)].x)-camera_x;
                base_y=battle_commander_y-camera_y+battle_view_center_y+15;
            } else {
                const int soldier_index=int(p.target_side)*200+p.owner_index;
                const auto& soldier=game.battle.soldiers[soldier_index];
                x=int(soldier.x)-camera_x;
                base_y=int(soldier.y)-camera_y+battle_view_center_y+15;
            }
            static constexpr uint8_t fire_frames[4]={0,1,2,1};
            const int age=76-int(p.remaining);
            const int frame=fire_frames[(age/8)&3];
            if(x+sg::magic_fire_hit_width/2>=field_left && x-sg::magic_fire_hit_width/2<field_right &&
               base_y>field_top && base_y-sg::magic_fire_hit_height<field_bottom)
                character_white_light(x-sg::magic_fire_hit_width/2,
                          base_y-sg::magic_fire_hit_height,
                          sg::magic_fire_hit_width,sg::magic_fire_hit_height,
                          sg::magic_fire_hit_sprites[frame],false);
        } else if(p.magic == 16 && p.phase==1) {
            const int arrow_y=screen_y-p.height-9;
            if(arrow_y+34>field_top && arrow_y+2<field_bottom)
                character_white_light(p.x-camera_x-16,arrow_y+2,sg::magic_arrow_sprite_size,
                          sg::magic_arrow_sprite_size,
                          sg::magic_arrow_sprites[sg::arrow_m002_fire_b],false);
        } else if(p.magic == 1 && p.phase != 0) {
            if(p.kind>=2) {
                const int x=int(p.x)-camera_x;
                const int draw_x=visual::centered_left(x,sg::magic_fire_hit_width);
                const int draw_y=visual::grounded_top(screen_y,sg::magic_fire_hit_height);
                if(draw_x+sg::magic_fire_hit_width>field_left && draw_x<field_right &&
                   draw_y+sg::magic_fire_hit_height>field_top &&
                   draw_y<field_bottom) {
                    // Fire feet stay on the radial point, like the commander;
                    // source bitmap padding is not a battle-world anchor.
                    character_white_light(draw_x,draw_y,
                              sg::magic_fire_hit_width,sg::magic_fire_hit_height,
                              sg::magic_fire_hit_sprites[0],false);
                }
            } else if(p.source_script>=2642 && p.source_script<=2644) {
                // Centered, raised sprites: their anchors are not bitmap origins.
                const int age=p.height;
                const int direction=p.dx>0 ? 1 : -1;
                const int head_x=int(p.x)-camera_x;
                const int raised_y=screen_y-24;
                // Stable segmented tail, without dozens of particle objects.
                for(int segment=3;segment>=0;--segment) {
                    if(age<segment*6 || age>75+segment*4) continue;
                    const int x=head_x-direction*segment*20;
                    if(x+24<field_left || x-24>=field_right ||
                       raised_y+24<=field_top || raised_y-24>=field_bottom) continue;
                    if(segment==0)
                        character_white_light(x-24,raised_y-24,48,48,
                            sg::magic_dragon_sprites[0],direction>0);
                    else {
                        const int frame=(age/3+segment)%6;
                        character_white_light(x-24,raised_y-16,48,32,
                            sg::magic_dragon_streaks[frame],direction>0);
                    }
                }
            } else if(screen_y+sg::magic_dragon_sprite_size>field_top &&
                      screen_y<field_bottom) {
                if(p.source_script>=2642 && p.source_script<=2644) {
                    const int age=int(p.height);
                    const int direction=p.dx<0 ? -1 : 1;
                    const int core_x=int(p.x)-camera_x;
                    const int child_count=minimum(9,age);
                    // MAGIC01 4501 starts one 4502 trail child per update;
                    // each child removes its B object at child age9. Show
                    // the overlapping nine-child tail instead of collapsing
                    // the child family into a single image.
                    for(int child_age=0;child_age<child_count;++child_age) {
                        if(child_age%3 && child_age!=8) continue;
                        // Each 4502 creates its B image at the core's current
                        // position and only changes its collision extents;
                        // it does not move that image afterward. The core
                        // advances16 native x units per update, so older
                        // children stay behind by16 units per child age.
                        const int tail_x=core_x-direction*(child_age*16*40/96);
                        // MAGIC01 4502 creates Things sequence4510, whose
                        // source definition is the static M015DragonB1 image.
                        const int trail_frame=0;
                        const int tail_y=screen_y;
                        if(tail_x+sg::magic_dragon_trail_width>=field_left &&
                           tail_x<field_right &&
                           tail_y+sg::magic_dragon_trail_height>field_top &&
                           tail_y<field_bottom)
                            character_white_light(tail_x-sg::magic_dragon_trail_x_offset[0],
                                      tail_y-sg::magic_dragon_trail_y_offset[0],
                                      sg::magic_dragon_trail_width,
                                      sg::magic_dragon_trail_height,
                                      sg::magic_dragon_trails[trail_frame],false);
                    }
                    // Every 4502 child creates sequence4520 at child age8 and
                    // refreshes native31(C,1). The native updater decrements
                    // object field +0x28 and deletes it at zero. Since 4501
                    // launches one child per update, represent the currently
                    // refreshed C wing once per core. Its resource comes from
                    // the launch parameters (+4 elevation,+2 angle each root
                    // update), indexed by the child that reached age8 now.
                    const int c_spawn_index=age>=9 ? age-9 : 0;
                    const int elevation=(c_spawn_index*4)&255;
                    const int elevation_delta=elevation>=128 ? elevation-128 : 128-elevation;
                    int streak_frame=elevation_delta<16 ? 4 :
                        (elevation_delta<48 ? 2 :
                         (elevation_delta<96 ? 0 :
                          (elevation_delta<112 ? 1 : 3)));
                    const int native_angle=(128+c_spawn_index*2)&255;
                    if(native_angle>=192 && native_angle<224) streak_frame=4;
                    else if(native_angle>170 && native_angle<192) streak_frame=5;
                    // The C child appears at B-child age8 at its spawn position. It has
                    // no later coordinate setter, so it stays at that point.
                    const int streak_x=core_x-direction*(8*16*40/96);
                    const int streak_y=screen_y;
                    if(age>=9 && streak_x+sg::magic_dragon_streak_width>=field_left &&
                       streak_x<field_right &&
                       streak_y+sg::magic_dragon_streak_height>field_top &&
                       streak_y<field_bottom) {
                        character_white_light(streak_x-sg::magic_dragon_streak_x_offset[streak_frame],
                                  streak_y-sg::magic_dragon_streak_y_offset[streak_frame],
                                  sg::magic_dragon_streak_width,
                                  sg::magic_dragon_streak_height,
                                  sg::magic_dragon_streaks[streak_frame],false);
                    }
                }
                const int frame=p.source_script>=2642 && p.source_script<=2644 ? 0 :
                    ((p.source_script>=2630 && p.source_script<=2632) ?
                     ((int(p.height)/2)&1) :
                     (((p.source_script>=2633 && p.source_script<=2635) ||
                       p.source_script==2660) ? int(p.height) :
                       (game.battle.tick/3)%3));
                magic_frame(p.kind ? 2646 : p.source_script,
                            frame,
                            p.x-camera_x,
                            screen_y+((p.source_script>=2630 &&
                                       p.source_script<=2632) ||
                                      (p.source_script>=2633 &&
                                       p.source_script<=2635) ||
                                      p.source_script==2660 ||
                                      (p.source_script>=2642 &&
                                       p.source_script<=2644) ||
                                      (p.source_script>=2645 &&
                                       p.source_script<=2647) ||
                                      p.source_script==2664 ? 0 : 18),
                            p.dx>0);
                if((p.source_script>=2645 && p.source_script<=2647) ||
                   p.source_script==2664) {
                    // GLOBAL4603 follows the M016 clone with one M011 4101
                    // wind trail at a native +48x,+32y relative offset.
                    const int trail_x=int(p.x)-camera_x+(p.dx<0 ? 20 : -20);
                    const int trail_y=visual::torso_y(screen_y)-sg::magic_m011_row_height/2;
                    const int trail_frame=(game.battle.tick/2)&1;
                    character_white_light(visual::centered_left(trail_x,sg::magic_m011_row_width),
                              trail_y,
                              sg::magic_m011_row_width,sg::magic_m011_row_height,
                              sg::magic_m011_row_sprites[trail_frame],p.dx>0);
                }
            }
        } else if(p.magic == 2) {
            if(p.phase == 1) {
                const int arrow_y = screen_y - p.height - 9;
                if(arrow_y+34 > field_top && arrow_y+2 < field_bottom) {
                    battle_arrow(p.x-camera_x,arrow_y,p.dx,1);
                }
            }
        } else if(p.magic == 3) {
            const int mine_y = screen_y - p.height;
            if(p.phase == 3 && mine_y>field_top &&
               mine_y-sg::magic_m005_mine_height<field_bottom) {
                const int age=original::minefield_visual_lifetime-int(p.remaining);
                const int frame=clamp(age/2,0,6); // sequence slot eight holds frame seven again.
                const int x=int(p.x)-camera_x;
                const int draw_x=x-sg::magic_m005_mine_x_offset[frame];
                const int draw_y=mine_y-sg::magic_m005_mine_y_offset[frame];
                if(draw_x+sg::magic_m005_mine_width>field_left && draw_x<field_right &&
                   draw_y+sg::magic_m005_mine_height>field_top &&
                   draw_y<field_bottom)
                    character_white_light(draw_x,draw_y,
                              sg::magic_m005_mine_width,sg::magic_m005_mine_height,
                              sg::magic_m005_mine_sprites[frame],false);
            }
        } else if(p.magic == 4) {
            const int wave_y = screen_y - p.height*battle_grid_row_height/72;
            const int wave_x = int(p.x)-camera_x;
            // Phase33 is the replacement sequence4401 child, not the final
            // frame of sequence4400. Do not leave the animated ascent frozen
            // on screen during the child's constructor/shared-variable wait.
            if(p.phase && p.phase<33 && wave_x+sg::magic_sword_sweep_size>field_left &&
               wave_x<field_right && wave_y+sg::magic_sword_sweep_size>field_top &&
               wave_y<field_bottom)
                magic_frame(p.source_script,
                            p.source_script>=2639 && p.source_script<=2641 ?
                                ((int(p.phase)-1)/2)&7 : (p.phase/2)%3,
                            wave_x,wave_y,false);
            // MAGIC01 4402's sequence4401 child is a static M014Sword1 image.
            if(p.phase>=33 && !p.scatter &&
               wave_x+sg::magic_sword_sweep_size>field_left && wave_x<field_right &&
               wave_y+sg::magic_sword_sweep_size>field_top &&
               wave_y<field_bottom)
                magic_frame(p.source_script,0,wave_x,wave_y,false);
        } else if(p.magic == 5) {
            // Each token is one complete, independently timed cyclone.
            // Bottom-center stays on its actual 100x100 damage footprint.
            const int x=int(p.x)-camera_x;
            for(int ring=0;ring<5;++ring) {
                const int frame=(int(p.height)/3+ring)%3;
                const int left=visual::centered_left(x,sg::magic_m013_cyclone_width);
                const int top=visual::grounded_top(screen_y,sg::magic_m013_cyclone_height)-ring*12;
                if(left+sg::magic_m013_cyclone_width<=field_left || left>=field_right ||
                   top+sg::magic_m013_cyclone_height<=field_top || top>=field_bottom) continue;
                const auto* pixels=ring ? sg::magic_m013_cyclone_b_sprites[frame] :
                    sg::magic_m013_cyclone_sprites[frame];
                character_white_light(left,top,sg::magic_m013_cyclone_width,
                    sg::magic_m013_cyclone_height,pixels,false);
            }
        } else if(p.magic == 6 && p.phase) {
            const int frame=p.kind&1;
            const int strike_x=int(p.x)-camera_x;
            const int strike_left=visual::centered_left(strike_x,sg::magic_strike_width);
            const int strike_top=visual::grounded_top(screen_y,sg::magic_strike_height);
            if(strike_left+sg::magic_strike_width>field_left && strike_left<field_right &&
               strike_top+sg::magic_strike_height>field_top &&
               strike_top<field_bottom)
                character_white_light(strike_left,
                          strike_top,sg::magic_strike_width,
                          sg::magic_strike_height,
                          sg::magic_strike_sprites[frame],
                          false);
        } else if(p.magic == 7 && p.kind && p.phase) {
            static constexpr uint8_t chariot_frames[14]={0,1,1,2,3,2,1,0,1,2,2,3,2,1};
            const int age=maximum(0,original::heaven_and_earth_beam_lifetime(p.source_script)-
                                    int(p.height));
            const int frame=chariot_frames[(age/4)%14];
            const int x=int(p.x)-camera_x;
            const int x_offset=sg::magic_chariot_x_offset[frame];
            const int draw_x=x-x_offset;
            const int draw_y=screen_y-sg::magic_chariot_y_offset[frame];
            if(draw_x+sg::magic_chariot_width>field_left && draw_x<field_right &&
               draw_y+sg::magic_chariot_height>field_top &&
               draw_y<field_bottom)
                character(draw_x,draw_y,
                          sg::magic_chariot_width,sg::magic_chariot_height,
                          sg::magic_chariot_sprites[frame],false);
        } else if(p.magic == 8 && p.phase) {
            if(p.source_script==2663) {
                // GLOBAL6230's final strike pair is Things 3920/3921, not
                // the M006 callback particles 3904/3905. The first B1/B2
                // image is visible immediately and #8 holds it; #9999 then
                // keeps it visible through the native38(16,1) tail.
                const int beam_frame=3+(p.owner_index&1);
                const int beam_x=int(p.x)-camera_x;
                const int beam_left=visual::centered_left(beam_x,sg::magic_lightning_width);
                const int beam_top=visual::grounded_top(screen_y,sg::magic_lightning_height);
                if(beam_left+sg::magic_lightning_width>field_left && beam_left<field_right &&
                   beam_top+sg::magic_lightning_height>field_top &&
                   beam_top<field_bottom)
                    character_white_light(beam_left,
                              beam_top,sg::magic_lightning_width,
                              sg::magic_lightning_height,
                              sg::magic_lightning_sprites[beam_frame],false);
            } else if(p.scatter<=2 && screen_y+visual::commander_feet>field_top &&
                      visual::grounded_top(screen_y,sg::magic_lightning_height)<field_bottom)
                magic_frame(3900,0,p.x-camera_x,screen_y,false);
        } else if(p.magic == 9 && p.phase == 1) {
            if(p.source_script==2626) {
                const int beam_frame=3+(p.kind&1);
                const int beam_x=int(p.x)-camera_x;
                const int beam_left=visual::centered_left(beam_x,sg::magic_lightning_width);
                const int beam_top=visual::grounded_top(screen_y,sg::magic_lightning_height);
                // Sequence3920/3921's #8 holds its first B image from object
                // creation; #9999 keeps it visible until native38 removes it.
                if(beam_left+sg::magic_lightning_width>field_left && beam_left<field_right &&
                   beam_top+sg::magic_lightning_height>field_top &&
                   beam_top<field_bottom)
                    character_white_light(beam_left,
                              beam_top,sg::magic_lightning_width,
                              sg::magic_lightning_height,
                              sg::magic_lightning_sprites[beam_frame],false);
            } else if(p.scatter<=2 && screen_y+visual::commander_feet>field_top &&
                      visual::grounded_top(screen_y,sg::magic_lightning_height)<field_bottom)
                magic_frame(3900,0,p.x-camera_x,screen_y,false);
        } else if(p.magic == 10 && p.phase != 2) {
            const int x=int(p.x)-camera_x, y=screen_y;
            if(p.source_script!=2665 && x>=field_left && x<field_right &&
               y>=field_top && y<field_bottom) {
                    const int pulse=(game.battle.tick+p.remaining)&1;
                    line(x-3,y,x+3,y,pulse ? int(pale) : int(ochre));
                    line(x,y-3,x,y+3,pulse ? teal : white);
                    pixel(x,y,white);
            }
        } else if(p.magic == 11 && p.phase == 1) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y-int(p.height)*battle_grid_row_height/72;
            if(p.source_script==2618 || p.source_script==2622 || p.source_script==2623) {
                // Things 3702/3703 and 3802/3803 draw M007Ston1 from the
                // object's raw top-left x/y; p.height supplies native z.
                if(x+sg::magic_m007_stone_size>=field_left && x<field_right &&
                   y+sg::magic_m007_stone_size>field_top &&
                   y<field_bottom)
                    character(x,y,sg::magic_m007_stone_size,
                              sg::magic_m007_stone_size,
                              sg::magic_m007_stone_sprite,false);
            } else if(x+24>=field_left && x-24<field_right && y+24>field_top && y-28<field_bottom) {
                magic_frame(p.source_script,(game.battle.tick/3)%3,x,y+18,false);
            }
        } else if((p.magic==28 || p.magic==29 || p.magic==30) && p.phase==1 &&
                  (p.source_script==2622 || p.source_script==2623)) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y;
            if(p.magic==28) {
                const int age=int(p.height);
                const int frame=minimum(5,(age ? age-1 : 0)/5);
                const int draw_x=x-sg::magic_m008_burst_x_offset[frame];
                const int draw_y=y-sg::magic_m008_burst_y_offset[frame];
                if(draw_x+sg::magic_m008_burst_width>=field_left && draw_x<field_right &&
                   draw_y+sg::magic_m008_burst_height>field_top &&
                   draw_y<field_bottom)
                    character_white_light(draw_x,draw_y,
                              sg::magic_m008_burst_width,sg::magic_m008_burst_height,
                              sg::magic_m008_burst_sprites[frame],false);
            } else if(p.magic==29) {
                const int frame=(int(p.height)/4)&1;
                const int draw_x=x-sg::magic_m008_impact_x_offset[frame];
                const int draw_y=y-sg::magic_m008_impact_y_offset[frame];
                if(draw_x+sg::magic_m008_impact_width>=field_left && draw_x<field_right &&
                   draw_y+sg::magic_m008_impact_height>field_top &&
                   draw_y<field_bottom)
                    character_white_light(draw_x,draw_y,
                              sg::magic_m008_impact_width,sg::magic_m008_impact_height,
                              sg::magic_m008_impact_sprites[frame],false);
            } else {
                const int field_y=y-int(p.height);
                const unsigned frame=p.kind%5;
                const int draw_x=x-sg::magic_m008_field_x_offset[frame];
                const int draw_y=field_y-sg::magic_m008_field_y_offset[frame];
                if(draw_x+sg::magic_m008_field_width>=field_left && draw_x<field_right &&
                   draw_y+sg::magic_m008_field_height>field_top &&
                   draw_y<field_bottom)
                    character_white_light(draw_x,draw_y,
                              sg::magic_m008_field_width,sg::magic_m008_field_height,
                              sg::magic_m008_field_sprites[frame],false);
            }
        } else if(p.magic==18 && (p.source_script==2664 ||
                                  (p.source_script>=2600 && p.source_script<=2605)) &&
                  p.phase==1 && p.height<10) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y;
            const int frame=p.kind ? 5 : 0; // Things 3004/3005 hold MANA1/MANB1 for ten updates.
            if(x+sg::magic_m001_mana_width/2>=field_left &&
               x-sg::magic_m001_mana_width/2<field_right &&
               y>field_top && y-sg::magic_m001_mana_height<field_bottom)
                character(x-sg::magic_m001_mana_width/2,
                          y-sg::magic_m001_mana_height+6,
                          sg::magic_m001_mana_width,sg::magic_m001_mana_height,
                          sg::magic_m001_mana_sprites[frame],false);
        } else if(p.magic==26 && (p.source_script==2664 ||
                                  (p.source_script>=2600 && p.source_script<=2605)) &&
                  p.phase==1) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y;
            const int age=int(p.height);
            const int local_frame=age<38 ? 0 : age<47 ? 1 : age<48 ? 2 : age<65 ? 3 : 4;
            const int frame=local_frame+(p.kind ? 5 : 0);
            if(x+sg::magic_m001_mana_width/2>=field_left &&
               x-sg::magic_m001_mana_width/2<field_right &&
               y>field_top && y-sg::magic_m001_mana_height<field_bottom)
                character(x-sg::magic_m001_mana_width/2,
                          y-sg::magic_m001_mana_height+6,
                          sg::magic_m001_mana_width,sg::magic_m001_mana_height,
                          sg::magic_m001_mana_sprites[frame],false);
        } else if(p.magic==19 && (p.source_script==2664 ||
                                  (p.source_script>=2600 && p.source_script<=2605)) &&
                  (p.phase==1 || p.phase==2 || p.phase==3)) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y;
            if(x+sg::magic_arrow_sprite_size/2>=field_left &&
               x-sg::magic_arrow_sprite_size/2<field_right &&
               y>field_top && y-sg::magic_arrow_sprite_size<field_bottom)
                character_white_light(x-sg::magic_arrow_sprite_size/2,
                          y-sg::magic_arrow_sprite_size/2,
                          sg::magic_arrow_sprite_size,sg::magic_arrow_sprite_size,
                          sg::magic_arrow_sprites[sg::arrow_m002_fire_c],false);
        } else if(p.magic==31 && p.source_script>=2600 && p.source_script<=2605 &&
                  p.phase==1) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y-p.height-9;
            const bool fire_mode=p.kind!=0;
            if(x+sg::magic_arrow_sprite_size/2>=field_left &&
               x-sg::magic_arrow_sprite_size/2<field_right &&
               y>field_top && y-sg::magic_arrow_sprite_size<field_bottom) {
                // Things sequence3002 has no OF_WHITELIGHT; only its paired
                // 3101 Fire1 child uses the white-light draw route.
                battle_arrow(x,y,p.dx,-1);
                if(fire_mode)
                    character_white_light(x-sg::magic_arrow_sprite_size/2,
                              y-sg::magic_arrow_sprite_size/2,
                              sg::magic_arrow_sprite_size,sg::magic_arrow_sprite_size,
                              sg::magic_arrow_sprites[sg::arrow_m002_fire_a],false);
            }
        } else if(p.magic==15 && (p.source_script==2664 || p.source_script==2618 ||
                                  p.source_script==2622 || p.source_script==2623) &&
                  (p.phase==1 ||
                   (p.source_script!=2664 && p.phase==3 && (p.kind&2)))) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y;
            int frame=0;
            const bool stone_fire=p.source_script==2618 || p.source_script==2622 || p.source_script==2623;
            bool visible=stone_fire ?
                (p.phase==3 && (p.kind&2)) : p.owner_index==0;
            const bool animated_cart=(stone_fire && p.phase==1 && !(p.kind&2)) ||
                (p.source_script==2664 && p.phase==1 && p.owner_index>0);
            if(animated_cart) {
                // Things.ini #38 holds the first M007RockA1 frame; it is
                // visible from sequence creation. The sequence then advances
                // to A2, holds the A2/sound slot for2, steps A3-A8 and keeps
                // the final A8 through its #16/#50 holds.
                const int age=int(p.height);
                visible=true;
                if(age<38) frame=0;
                else if(age<41) frame=1;
                else if(age<42) frame=2;
                else if(age<43) frame=3;
                else if(age<44) frame=4;
                else if(age<45) frame=5;
                else if(age<46) frame=6;
                else frame=7;
            }
            const int draw_x=x-sg::magic_m007_catapult_x_offset[frame];
            const int draw_y=y-sg::magic_m007_catapult_y_offset[frame];
            if(visible && draw_x+sg::magic_m007_catapult_size>=field_left && draw_x<field_right &&
               draw_y+sg::magic_m007_catapult_size>field_top &&
               draw_y<field_bottom)
                character(draw_x,draw_y,
                          sg::magic_m007_catapult_size,sg::magic_m007_catapult_size,
                          sg::magic_m007_catapult_sprites[frame],false);
        } else if(p.magic==13 || p.magic==14) {
            const int x=int(p.x)-camera_x;
            const int y=screen_y;
            if(p.magic==13) {
                if(p.source_script==2665) {
                    const int visual_y=y-int(p.height)*battle_grid_row_height/72;
                    const unsigned sprite=p.kind&3;
                    // Native image drawing anchors each Things bitmap's top-left
                    // at the object position; compensate for the transparent
                    // padding added while fitting M022 art into a 28px canvas.
                    const int draw_x=x-sg::magic_wave_sprite_x_offset[sprite];
                    const int draw_y=visual_y-sg::magic_wave_sprite_y_offset[sprite];
                    if(draw_x+sg::magic_wave_sprite_size>=field_left && draw_x<field_right &&
                       draw_y+sg::magic_wave_sprite_size>field_top &&
                       draw_y<field_bottom)
                        character_white_light(draw_x,draw_y,
                                  sg::magic_wave_sprite_size,sg::magic_wave_sprite_size,
                                  sg::magic_wave_sprites[sprite],false);
                } else if(p.kind==0) {
                    const int age=maximum(0,int(p.height)-1);
                    const int frame=p.source_script==2659 ? clamp(age/2,0,3) :
                        clamp(age/2,0,2);
                    const int sprite=p.source_script==2659 ? 3+frame : frame;
                    const int draw_x=visual::centered_left(x,sg::magic_m020_field_size);
                    const int draw_y=visual::grounded_top(y,sg::magic_m020_field_size);
                    if(draw_x+sg::magic_m020_field_size>=field_left && draw_x<field_right &&
                       draw_y+sg::magic_m020_field_size>field_top &&
                       draw_y<field_bottom)
                        character_white_light(draw_x,draw_y,
                                  sg::magic_m020_field_size,sg::magic_m020_field_size,
                                  sg::magic_m020_field_sprites[sprite],false);
                } else if(p.kind==1) {
                    // GLOBAL6000's second GLOBAL2511 child uses sequence
                    // 6002/6003: M020swc1 stays visible for two/three #2
                    // frames before the source sequence switches to NULSHAPE.
                    const int visible_updates=p.source_script==2659 ? 6 : 4;
                    const int sprite=7;
                    const int draw_x=visual::centered_left(x,sg::magic_m020_field_size);
                    const int draw_y=visual::grounded_top(y,sg::magic_m020_field_size);
                    if(int(p.height)<=visible_updates &&
                       draw_x+sg::magic_m020_field_size>=field_left && draw_x<field_right &&
                       draw_y+sg::magic_m020_field_size>field_top &&
                       draw_y<field_bottom)
                        character_white_light(draw_x,draw_y,
                                  sg::magic_m020_field_size,sg::magic_m020_field_size,
                                  sg::magic_m020_field_sprites[sprite],false);
                }
            } else if(p.remaining) {
                const int age=int(p.height);
                const int speed=p.owner_index==7 ? 7 : 6;
                const int vertical_speed_q16=
                    magic_native_vertical_velocity_q16(speed,int(p.attacker_rear));
                // native29(10752) subtracts gravity after each position
                // update. Convert the resulting native z through the 30/72
                // battle-row scale; clamp below-ground fragments at the floor.
                const int z_q16=vertical_speed_q16*age-
                    10752*age*(age-1);
                const int z=maximum(0,z_q16*30/(65536*72));
                const int debris_y=y-z;
                const int sprite=8+(p.kind%6);
                const int draw_x=visual::centered_left(x,sg::magic_m020_field_size);
                const int draw_y=visual::grounded_top(debris_y,sg::magic_m020_field_size);
                if(draw_x+sg::magic_m020_field_size>=field_left && draw_x<field_right &&
                   draw_y+sg::magic_m020_field_size>field_top &&
                   draw_y<field_bottom)
                    character_white_light(draw_x,draw_y,
                              sg::magic_m020_field_size,sg::magic_m020_field_size,
                              sg::magic_m020_field_sprites[sprite],false);
            } else {
                const int sprite=8+(p.kind%6);
                const int draw_x=visual::centered_left(x,sg::magic_m020_field_size);
                const int draw_y=visual::grounded_top(y,sg::magic_m020_field_size);
                if(draw_x+sg::magic_m020_field_size>=field_left && draw_x<field_right &&
                   draw_y+sg::magic_m020_field_size>field_top &&
                   draw_y<field_bottom)
                    character_white_light(draw_x,draw_y,
                              sg::magic_m020_field_size,sg::magic_m020_field_size,
                              sg::magic_m020_field_sprites[sprite],false);
            }
        } else if(p.magic==12 && p.source_script==2650 && p.phase==1) {
            const int x=int(p.x)-camera_x;
            const int glyph_index=p.kind&7;
            const int draw_x=x-sg::magic_eight_gates_x_offset[glyph_index];
            const int draw_y=screen_y-sg::magic_eight_gates_y_offset[glyph_index];
            if(draw_x+sg::magic_eight_gates_size>field_left && draw_x<field_right &&
               draw_y+sg::magic_eight_gates_size>field_top &&
               draw_y<field_bottom)
                character_white_light(draw_x,draw_y,
                          sg::magic_eight_gates_size,sg::magic_eight_gates_size,
                          sg::magic_eight_gates_sprites[glyph_index],false);
        } else if(p.magic==0) battle_arrow(p.x-camera_x,screen_y,p.dx,0);
        else warbar_frame(p.kind, p.x-camera_x, screen_y);
    }
    mark_stage(118);
    if(game.battle.effect_ticks && game.battle.pending_winner < 0) {
        const bool allied=game.battle.effect_kind==1 || game.battle.effect_kind==2;
        const int target_side=allied ? game.battle.effect_side : 1-game.battle.effect_side;
        const int ex = game.battle.sides[target_side].x - camera_x;
        const int effect_y = battle_commander_y-camera_y+battle_view_center_y;
        if(!original::magic_uses_collision_projectile(game.battle.effect_script) &&
           !original::magic_uses_tornado(game.battle.effect_script) &&
           !original::magic_uses_scripted_field(game.battle.effect_script) &&
           !original::sword_wave_projectile_count(game.battle.effect_script) &&
           !original::magic_uses_heaven_and_earth(game.battle.effect_script) &&
           !original::magic_uses_fire_wheel(game.battle.effect_script) &&
           !original::magic_action_delay(game.battle.effect_script) &&
           game.battle.effect_script!=2666 && game.battle.effect_script!=2667 &&
           !original::reinforcement_amount(game.battle.effect_script) &&
           !original::lightning_strike_count(game.battle.effect_script) &&
           !original::magic_uses_arrow_volley(game.battle.effect_script) &&
           !original::rockfall_strike_count(game.battle.effect_script) &&
           !original::minefield_count(game.battle.effect_script) &&
           game.battle.effect_script!=2617 &&
           !original::stationary_callback_object_count(game.battle.effect_script) &&
           !original::chariot_visual_count(game.battle.effect_script) &&
           !original::magic_uses_ghost_crying_god(game.battle.effect_script) &&
           !original::healing_amount(game.battle.effect_script)) {
            const int effect_duration = original::magic_effect_launch_delay(game.battle.effect_script);
            const int effect_elapsed = effect_duration - int(game.battle.effect_ticks);
            const int effect_frame = clamp(effect_elapsed * 3 / effect_duration,0,2);
            magic_frame(game.battle.effect_script,effect_frame,ex,effect_y+20);
        }
    }
    project_field=false;
    clip_top=0; clip_bottom=160;
    const int remaining_updates=maximum(0,original::battle_timeout_updates-int(game.battle.duel_tick));
    rect(110,146,34,12,surface);
    text(110,146,"时",muted_ink);
    number(122,146,(remaining_updates+29)/30,remaining_updates<=900 ? cinnabar : text_ink);
    mark_stage(119);
    const auto& controlled_fighter = game.battle.sides[controlled];
    const bool reinforcement_full=game.battle.effect_remaining &&
        game.battle.effect_side==controlled &&
        original::reinforcement_amount(game.battle.effect_script) && game.battle.magic_field_state==1;
    const char* skill_status = game.battle.pending_winner >= 0 ? "战斗结束" :
        reinforcement_full ? "兵力已满" :
        game.battle.effect_remaining ? (game.battle.effect_side==controlled ? "我方施技" : "敌方施技") :
        controlled_fighter.order == Order::retreat ? "撤退中" :
        !skill || controlled_fighter.mp < skill->mp ? "技力不足" :
        controlled_fighter.charge < original::cast_charge_max ? "集气中" : "A可施放";
    const int status_color = game.battle.pending_winner >= 0 ? muted_ink :
        game.battle.effect_remaining ? ochre :
        controlled_fighter.order == Order::retreat ? muted_ink :
        !skill || controlled_fighter.mp < skill->mp ? cinnabar :
        controlled_fighter.charge < original::cast_charge_max ? text_ink : ochre;
    rect(146,146,94,12,surface);
    text(146,146,skill_status,status_color);
    auto toast=[&](const char* message,int color) {
        const int width=minimum(180,text_width(message)+8);
        const bool mirrored=battle_view_mirrored(game);
        const int x=mirrored ? 4 : 236-width;
        constexpr int y=128,height=14;
        rect(x,y,width,height,surface);
        ui_text(x+4,y+1,message,color,width-8);
        // Counter-flip only this small UI patch before the hardware field
        // mirror. Its final position is screen-right and glyphs stay upright.
        if(mirrored) {
            auto* pixels=reinterpret_cast<uint8_t*>(screen_tiles);
            for(int yy=y;yy<y+height;++yy)
                for(int col=0;col<width/2;++col) {
                    const int left=x+col,right=x+width-1-col;
                    const int row=(yy>>3)*30*64+(yy&7)*8;
                    auto& a=pixels[row+(left>>3)*64+(left&7)];
                    auto& b=pixels[row+(right>>3)*64+(right&7)];
                    const uint8_t saved=a; a=b; b=saved;
                }
        }
    };
    if(notice && *notice) toast(notice,cinnabar);
    if(game.battle.pending_winner >= 0) {
        const char* result = game.battle.pending_winner == 2 ? "本阵超时平局" :
            game.battle.pending_winner == controlled ? "胜利" : "失败";
        if(game.battle.effect_ticks>=15) toast(result,ochre);
    }
    mark_stage(120);
    combat_active=true;
    // Flip field tiles in hardware. Text and HUD stay upright, while actors,
    // projectiles and terrain share the same player-left projection at no
    // per-frame pixel-copy cost. Menus restore the ordinary map in clear().
    battle_orientation(battle_view_mirrored(game));
}

void Render::present(bool battle) {
    if(!battle) { combat_active=false; upload_full=true; }
    upload_pending=true;
}
}

