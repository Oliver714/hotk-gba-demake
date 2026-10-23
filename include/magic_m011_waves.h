#ifndef SANGO_MAGIC_M011_WAVES_H
#define SANGO_MAGIC_M011_WAVES_H
#include <stdint.h>
namespace sg {
constexpr int magic_m011_wave_width=40;
constexpr int magic_m011_wave_height=56;
extern const uint8_t magic_m011_wave_sprites[2][2240];
extern const uint8_t magic_m011_wave_x_offset[2];
extern const uint8_t magic_m011_wave_flipped_x_offset[2];
extern const uint8_t magic_m011_wave_y_offset[2];
}
#endif
