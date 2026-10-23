#ifndef SANGO_MAGIC_M013_CYCLONE_H
#define SANGO_MAGIC_M013_CYCLONE_H
#include <stdint.h>
namespace sg {
constexpr int magic_m013_cyclone_width=48;
constexpr int magic_m013_cyclone_height=24;
extern const uint8_t magic_m013_cyclone_sprites[3][1152];
extern const uint8_t magic_m013_cyclone_b_sprites[3][1152];
extern const uint8_t magic_m013_cyclone_x_offset[3];
extern const uint8_t magic_m013_cyclone_flipped_x_offset[3];
extern const uint8_t magic_m013_cyclone_y_offset[3];
extern const uint8_t magic_m013_cyclone_b_x_offset[3];
extern const uint8_t magic_m013_cyclone_b_flipped_x_offset[3];
extern const uint8_t magic_m013_cyclone_b_y_offset[3];
}
#endif
