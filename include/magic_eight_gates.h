#ifndef SANGO_MAGIC_EIGHT_GATES_H
#define SANGO_MAGIC_EIGHT_GATES_H
#include <stdint.h>
namespace sg {
constexpr int magic_eight_gates_size=32;
extern const uint8_t magic_eight_gates_x_offset[8];
extern const uint8_t magic_eight_gates_y_offset[8];
extern const uint8_t magic_eight_gates_sprites[8][1024];
}
#endif
