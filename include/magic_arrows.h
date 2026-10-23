#ifndef SANGO_MAGIC_ARROWS_H
#define SANGO_MAGIC_ARROWS_H
#include <stdint.h>
namespace sg {
constexpr int magic_arrow_sprite_size=32;
enum MagicArrowSprite : uint8_t { arrow_m001_down, arrow_m002_fire_a,
    arrow_m002_fire_b, arrow_m002_fire_c, arrow_m001_up };
extern const uint8_t magic_arrow_sprites[5][1024];
}
#endif
