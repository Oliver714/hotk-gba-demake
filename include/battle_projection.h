#ifndef SANGO_BATTLE_PROJECTION_H
#define SANGO_BATTLE_PROJECTION_H
#include "model.h"
namespace sg {
// Power-of-two projection needs no divide in the per-pixel render loops.
struct BattleProjection {
    int shift=0;
    int x(int value) const { return 120+((value-120)>>shift); }
    int y(int value) const { return battle_view_center_y+((value-battle_view_center_y)>>shift); }
    int left() const { return 120-(120<<shift); }
    int right() const { return 120+(120<<shift); }
    int top() const { return battle_view_center_y-((battle_view_center_y-battle_view_top)<<shift); }
    int bottom() const { return battle_view_center_y+((battle_view_bottom-battle_view_center_y)<<shift); }
    static int fit_shift(int width,int height) {
        int result=0;
        while(result<5 && (width>(battle_view_width<<result) || height>(battle_view_height<<result))) ++result;
        return result;
    }
};
}
#endif
