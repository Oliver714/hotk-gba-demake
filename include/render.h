#ifndef SANGO_RENDER_H
#define SANGO_RENDER_H
#include "bn_regular_bg_ptr.h"
#include "bn_regular_bg_tiles_ptr.h"
#include "bn_regular_bg_map_cell.h"
#include "bn_tile.h"
#include "model.h"
#include "packed_sprites.h"
#include "ui_theme.h"
namespace sg {
enum Ink { background, white, gold, red, blue, green, ink, road, dark_green, tan, brown, pale, teal, purple, orange, gray };
class Render {
public:
    Render();
    void clear(int color = background);
    BN_CODE_IWRAM void pixel(int x, int y, int color);
    BN_CODE_IWRAM void rect(int x, int y, int w, int h, int color);
    void ui_gradient(int x, int y, int w, int h, int top, int bottom, int ramp = 0);
    void ui_cursor(int x, int y, int color);
    void line(int x, int y, int x2, int y2, int color);
    BN_CODE_IWRAM void text(int x, int y, const char* string, int color = white);
    void number(int x, int y, int value, int color = white);
    int text_width(const char* string, bool heading = false) const;
    BN_CODE_IWRAM void ui_text(int x,int y,const char* string,int color=text_ink,int width=240);
    void heading(int x,int y,const char* string,int width=220);
    void ui_number(int x,int y,int value,int color=text_ink);
    void ui_unsigned(int x,int y,uint32_t value,int color=text_ink);
    void ui_amount(int x,int y,uint32_t value);
    void ui_center(int x,int y,int width,const char* string,int color=text_ink);
    void ui_message(int x,int y,int width,const char* string,int color=warning_ink);
    void ui_window(int x,int y,int w,int h,bool selected=false);
    void ui_box(int x,int y,int w,int h,bool selected=false);
    void footer(const char* first,const char* second=nullptr);
    void page_marker(int current,int total,int y=24);
    void portrait_large(int x,int y,int general);
    void city_scene(int x,int y,int owner);
    void map_city(int x,int y,int color,bool zoomed,bool occupied,
                  const char* owner_name=nullptr,int letter_ink=text_ink);
    void ui_bitmap(int x,int y,int w,int h,const uint8_t* pixels);
    void ui_glyph(int x,int y,const UiGlyph& glyph,int height,int color);
    void bar(int x, int y, int w, int value, int total, int color);
    void panel(const char* title);
    void title_main(int selected, const char* message = nullptr, uint32_t animation_frame = 0);
    void title_leaves(uint32_t animation_frame,uint32_t previous_frame);
    void title(const char* scenario, int selected, const char* message = nullptr);
    void row(int y, const char* label, bool selected, int color = white);
    void button(int x, int y, int w, const char* label, bool selected, int color = white);
    void compact_button(int x, int y, int w, const char* label, bool selected);
    void portrait(int x, int y, int general, int color);
    void small_portrait(int x, int y, int general, int frame_color);
    void bitmap(int x, int y, int w, int h, const uint8_t* pixels);
    void fullscreen_bitmap(const uint8_t* pixels);
    BN_CODE_IWRAM void character(int x, int y, int w, int h, const uint8_t* pixels, bool flip);
    void character_white_light(int x,int y,int w,int h,const uint8_t* pixels,bool flip,
                               bool half_mix=false);
    void effect_white_light_rows(int x,int y,int w,int h,const uint8_t* pixels,
                                 const uint8_t* row_spans,bool half_mix=false,bool flip=false);
    void city_frame(int x, int y);
    void city_frame_b(int x, int y);
    void city_command_frame(int x, int y);
    void formation_frame(int x, int y);
    void troop_type_frame(int x, int y);
    void officer_skill_frame(int x, int y);
    void major_frame(int x, int y);
    void commander_frame(int x, int y);
    void assemble_frame(int x, int y);
    void officer_list_frame(int x, int y);
    void city_button(int x, int y, int button, bool selected, bool enabled = true);
    void magic_frame(int script, int frame, int x, int y, bool flip = false);
    void warbar_frame(int kind, int x, int y);
    void battle_arrow(int x,int y,int velocity_x,int velocity_y);
    void battle_frame();
    void battle_general_headers(const Game& game,int controlled);
    void map(const Game& game, int selected, bool paused, int selection_color = -1, const char* notice = nullptr, bool zoomed = false, int speed = 1, bool compact = false, int focused_army = -1);
    void destinations(const Game& game, int origin, int selected, const char* notice = nullptr, int speed = 1, bool zoomed = false);
    void target_cities(const Game& game, int origin, const int* options, int count,
                       int selected, bool dispatch, const char* notice = nullptr, int speed = 1, bool zoomed = false, int army = -1);
    void combat(Game& game, int skill_slot, const char* notice = nullptr, bool fit_all = false);
    void present(bool battle = false);
private:
    void magic_m012_frame(int script,int frame,int x,int y);
    void battle_orientation(bool mirrored);
    void battle_commander(const Game& game,int side,int camera_x,int camera_y);
    void faction_palette(bool enabled);
    BN_CODE_IWRAM void packed_character(int x,int y,const PackedSprite& sprite);
    BN_CODE_IWRAM void battle_terrain(int camera_x,int camera_y,int ground);
    BN_CODE_IWRAM void battle_actors(const Game& game,int camera_x,int camera_y);
    bn::regular_bg_tiles_ptr _tiles;
    bn::regular_bg_ptr _bg;
};
}
#endif
