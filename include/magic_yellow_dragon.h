#ifndef SANGO_MAGIC_YELLOW_DRAGON_H
#define SANGO_MAGIC_YELLOW_DRAGON_H
#include <stdint.h>
namespace sg {
constexpr int yellow_dragon_width=72;
constexpr int yellow_dragon_height=100;
constexpr int yellow_dragon_orb_size=24;
constexpr int yellow_dragon_flame_width=32;
constexpr int yellow_dragon_flame_height=112;
extern const uint8_t yellow_dragon_sprite[7200];
extern const uint8_t yellow_dragon_orbs[5][576];
extern const uint8_t yellow_dragon_flame[3584];
extern const uint8_t yellow_dragon_x_offset;
extern const uint8_t yellow_dragon_flipped_x_offset;
extern const uint8_t yellow_dragon_y_offset;
extern const uint8_t yellow_dragon_orb_x_offset[5];
extern const uint8_t yellow_dragon_orb_y_offset[5];
extern const uint8_t yellow_dragon_flame_x_offset;
extern const uint8_t yellow_dragon_flame_y_offset;
constexpr int yellow_dragon_fire_width[3]={10,16,22};
constexpr int yellow_dragon_fire_height[3]={44,72,98};
extern const uint8_t yellow_dragon_fire_0[440];
extern const uint8_t yellow_dragon_fire_1[1152];
extern const uint8_t yellow_dragon_fire_2[2156];
extern const uint8_t yellow_dragon_fire_x_offset[3];
extern const uint8_t yellow_dragon_fire_y_offset[3];
}
#endif
