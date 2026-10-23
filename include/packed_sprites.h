#ifndef SANGO_PACKED_SPRITES_H
#define SANGO_PACKED_SPRITES_H
#include <stdint.h>
namespace sg {
struct SpriteSpan { uint32_t pixels, mask; uint32_t column; };
struct PackedSprite { const SpriteSpan* spans; const uint16_t* rows; int height; };
extern const PackedSprite packed_cavalry[2][7][2][4];
extern const PackedSprite packed_infantry[2][4][5][2][4];
}
#endif
