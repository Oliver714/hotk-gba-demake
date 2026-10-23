#ifndef SANGO_BATTLE_VISUAL_RULES_H
#define SANGO_BATTLE_VISUAL_RULES_H
namespace sg::visual {
// Same ground reference as Render::battle_commander's horse/shadow.
constexpr int commander_feet=15;
constexpr int torso_y(int ground_y) { return ground_y-12; }
constexpr int grounded_top(int ground_y,int height) { return ground_y+commander_feet-height; }
constexpr int centered_left(int x,int width) { return x-width/2; }
constexpr int travel_sign(int value) { return value<0 ? -1 : value>0 ? 1 : 0; }
}
#endif
