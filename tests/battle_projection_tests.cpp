#include "battle_projection.h"
#include <cassert>
#include <cstdio>
int main() {
    for(int width=1;width<=3000;width+=7) for(int height=1;height<=1000;height+=7) {
        sg::BattleProjection p;
        p.shift=sg::BattleProjection::fit_shift(width+16,height+16);
        const int x=120-width/2, y=sg::battle_view_center_y-height/2;
        assert(p.x(x)>=0 && p.x(x+width)<240);
        assert(p.y(y)>=sg::battle_view_top && p.y(y+height)<sg::battle_view_bottom);
        assert(p.x(p.left())==0 && p.x(p.right())==240);
        assert(p.y(p.top())==sg::battle_view_top && p.y(p.bottom())==sg::battle_view_bottom);
    }
    assert(sg::BattleProjection::fit_shift(240,96)==0);
    assert(sg::BattleProjection::fit_shift(241,96)==1);
    assert(sg::BattleProjection::fit_shift(2240,440)==4);
    std::puts("Battle projection bounds passed");
}
