#ifndef SANGO_MAGIC_STRIKES_H
#define SANGO_MAGIC_STRIKES_H
#include <stdint.h>
namespace sg {
constexpr int magic_strike_width=44;
constexpr int magic_strike_height=88;
extern const uint8_t magic_strike_sprites[2][3872];
extern const uint8_t magic_strike_x_offset[2];
extern const uint8_t magic_strike_flipped_x_offset[2];
extern const uint8_t magic_strike_y_offset[2];
}
#endif
