#ifndef SANGO_UI_THEME_H
#define SANGO_UI_THEME_H
#include "data.h"
namespace sg {
// New design v1 (assets/ui/new-design-v1/theme.json). The sixteen roles keep
// their original order so every call site is unchanged; only the colours moved
// from warm paper to deep navy with gold edges, vermilion selection and cream
// text. paper/surface/light_ink/focus_paper double as the navy sheen ramp that
// ui_gradient(..., ramp=1) dithers between.
enum UiInk { paper=166, surface, text_ink, muted_ink, rule_ink, cinnabar,
    indigo, jade, ochre, focus_paper, light_ink, warning_ink, pale_paper,
    shadow_ink, seal_ink, disabled_ink };
// Palette slots 246-255. paper_hi..paper_deep are the warm ramp kept for light
// detail panels (ui_gradient ramp 0); the rest are window bevel and gold accents.
enum UiTone { paper_hi=246, paper_mid, paper_lo, paper_deep, edge_dark,
    edge_light, gold_hi, gold_lo, shadow_soft, paper_base };
struct UiGlyph { uint32_t codepoint; uint16_t rows[16]; uint8_t width; };
extern const UiGlyph ui_glyphs[], heading_glyphs[];
extern const int ui_glyph_count, heading_glyph_count;
extern const uint8_t silk_title_pixels[240*160], silk_logo_pixels[144*32];
constexpr int paper_map_palette = 230;
constexpr int faction_palette_first = 182;
constexpr int faction_palette_count = max_kings;
constexpr int map_field_top = 19;
constexpr int map_field_height = 112;
constexpr int map_compact_field_height = 96;
const char* ui_force_name(int force);
const char* ui_formation_name(int formation);
}
#endif
