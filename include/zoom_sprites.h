#ifndef SANGO_ZOOM_SPRITES_H
#define SANGO_ZOOM_SPRITES_H
#include <stdint.h>
#include "packed_sprites.h"
namespace sg {
struct ZoomSprite { const uint8_t* pixels; int width,height; };
extern const ZoomSprite zoom_cavalry[5][2][7];
extern const ZoomSprite zoom_infantry[5][2][4][5];
extern const PackedSprite packed_zoom_infantry[2][4][5][2][4];
}
#endif
