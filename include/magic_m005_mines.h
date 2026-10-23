#ifndef SANGO_MAGIC_M005_MINES_H
#define SANGO_MAGIC_M005_MINES_H
#include <stdint.h>
namespace sg {
constexpr int magic_m005_mine_width=40;
constexpr int magic_m005_mine_height=64;
extern const uint8_t magic_m005_mine_x_offset[7];
extern const uint8_t magic_m005_mine_y_offset[7];
extern const uint8_t magic_m005_mine_sprites[7][2560];
}
#endif
