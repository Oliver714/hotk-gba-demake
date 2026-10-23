#ifndef SANGO_MAGIC_M008_STONEFIRE_H
#define SANGO_MAGIC_M008_STONEFIRE_H
#include <stdint.h>
namespace sg {
constexpr int magic_m008_burst_width=64;
constexpr int magic_m008_burst_height=64;
extern const uint8_t magic_m008_burst_sprites[6][4096];
extern const uint8_t magic_m008_burst_x_offset[6];
extern const uint8_t magic_m008_burst_y_offset[6];
constexpr int magic_m008_impact_width=32;
constexpr int magic_m008_impact_height=40;
extern const uint8_t magic_m008_impact_sprites[2][1280];
extern const uint8_t magic_m008_impact_x_offset[2];
extern const uint8_t magic_m008_impact_y_offset[2];
constexpr int magic_m008_field_width=16;
constexpr int magic_m008_field_height=16;
extern const uint8_t magic_m008_field_sprites[5][256];
extern const uint8_t magic_m008_field_x_offset[5];
extern const uint8_t magic_m008_field_y_offset[5];
}
#endif
