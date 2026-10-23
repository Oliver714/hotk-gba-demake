#ifndef SANGO_MAGIC_LIGHTNING_H
#define SANGO_MAGIC_LIGHTNING_H
#include <stdint.h>
namespace sg {
constexpr int magic_lightning_width=44;
constexpr int magic_lightning_height=96;
extern const uint8_t magic_lightning_sprites[5][4224];
extern const uint8_t magic_lightning_x_offset[5];
extern const uint8_t magic_lightning_flipped_x_offset[5];
extern const uint8_t magic_lightning_y_offset[5];
}
#endif
