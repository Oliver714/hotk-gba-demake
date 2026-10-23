#ifndef SANGO_MAGIC_DRAGON_H
#define SANGO_MAGIC_DRAGON_H
#include <stdint.h>
namespace sg {
constexpr int magic_dragon_sprite_size=48;
constexpr int magic_dragon_trail_width=32;
constexpr int magic_dragon_trail_height=48;
constexpr int magic_dragon_streak_width=48;
constexpr int magic_dragon_streak_height=32;
extern const uint8_t magic_dragon_sprites[1][2304];
extern const uint8_t magic_dragon_trails[2][1536];
extern const uint8_t magic_dragon_streaks[6][1536];
extern const uint8_t magic_dragon_core_x_offset[1];
extern const uint8_t magic_dragon_core_flipped_x_offset[1];
extern const uint8_t magic_dragon_core_y_offset[1];
extern const uint8_t magic_dragon_trail_x_offset[2];
extern const uint8_t magic_dragon_trail_flipped_x_offset[2];
extern const uint8_t magic_dragon_trail_y_offset[2];
extern const uint8_t magic_dragon_streak_x_offset[6];
extern const uint8_t magic_dragon_streak_flipped_x_offset[6];
extern const uint8_t magic_dragon_streak_y_offset[6];
}
#endif
