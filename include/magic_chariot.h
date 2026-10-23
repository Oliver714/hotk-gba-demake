#ifndef SANGO_MAGIC_CHARIOT_H
#define SANGO_MAGIC_CHARIOT_H
#include <stdint.h>
namespace sg {
constexpr int magic_chariot_width=80;
constexpr int magic_chariot_height=36;
extern const uint8_t magic_chariot_sprites[4][2880];
extern const uint8_t magic_chariot_x_offset[4];
extern const uint8_t magic_chariot_flipped_x_offset[4];
extern const uint8_t magic_chariot_y_offset[4];
}
#endif
