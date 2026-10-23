#define private public
#include "model.h"
#undef private
#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include "battle_camera.h"
#include "battle_visual_rules.h"
#include <algorithm>
#include <initializer_list>

int main() {
    int16_t heads[sg::battle_grid_rows*(sg::battle_grid_columns+1)],next[400];
    std::fill(std::begin(heads),std::end(heads),int16_t(-1));
    std::fill(std::begin(next),std::end(next),int16_t(-1));
    for(int id: {53,65}) for(int side=0;side<2;++side) {
        prepare_skill_review(id,side,10);
        const int center=game.battle.sides[side].x;
        sg::Projectile* fire[8]{};
        int count=0;
        for(auto& p:game.battle.projectiles) if(p.active && p.magic==1 && p.kind>=2) {
            assert(p.x==center && p.y==sg::battle_commander_y);
            fire[count++]=&p;
        }
        assert(count==8);
        for(int tick=0;tick<36;++tick) {
            game.battle.sides[side].x=int16_t(center+400+tick*3);
            int camera_x=0,camera_y=0;
            assert(sg::visual::firewheel_focus(game.battle,camera_x,camera_y));
            assert(camera_x==center && camera_y==sg::battle_commander_y);
            int sum_x=0,sum_y=0;
            for(auto* p:fire) {
                game.update_firewheel_projectile(*p,heads,next);
                assert(p->target_offset_x==center && p->height==sg::battle_commander_y);
                assert(sg::visual::centered_left(p->x,32)+16==p->x);
                assert(sg::visual::grounded_top(p->y,40)+40==p->y+15);
                sum_x+=p->x;sum_y+=p->y;
            }
            assert(std::abs(sum_x-center*8)<=8);
            assert(std::abs(sum_y-sg::battle_commander_y*8)<=8);
        }
        int x=0,y=0;
        assert(!sg::visual::firewheel_focus(game.battle,x,y));
        for(const auto* p:fire) assert(!p->active);
    }
    std::puts("PASS firewheel: both skills/sides; moved caster leaves fixed center, symmetric radial paths, bottom-center sprite anchors, camera center and cleanup");
}
