#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include "battle_camera.h"
#include <cstring>
#include <initializer_list>

int main() {
    const int ids[4]={26,31,60,77},amounts[4]={4,8,16,30};
    for(int tier=0;tier<4;++tier) for(int side=0;side<2;++side)
        for(int initial: {0,20,95,100}) {
            prepare_skill_review(ids[tier],side,initial);
            // An occupied entry cell must be skipped, not overwritten.
            auto& blocker=game.battle.soldiers[(1-side)*200];
            if(initial>0) {
                blocker.x=blocker.previous_x=int16_t(side ? 80 : sg::battlefield_width-80);
                blocker.y=blocker.previous_y=60;
            }
            const auto& f=game.battle.sides[side];
            const int front_before=f.front;
            const int expected=sg::minimum(amounts[tier],100-initial);
            if(!expected) assert(std::strcmp(game.notice,"兵力已满")==0);
            for(int tick=0;tick<20;++tick) game.battle_tick();
            assert(f.soldiers==initial+expected);
            const int cavalry_before=initial-front_before;
            const int cavalry_added=sg::minimum(expected,50-cavalry_before);
            assert(f.front==front_before+expected-cavalry_added);
            for(int i=initial;i<initial+expected;++i) {
                const auto& s=game.battle.soldiers[side*200+i];
                assert(s.hp>0 && s.side==side && s.index==i);
                assert(side ? s.x<480 : s.x>sg::battlefield_width-480);
                assert(s.rear==uint8_t(i-initial<cavalry_added));
                if(initial>0) assert(s.x!=blocker.x || s.y!=blocker.y);
                for(int j=initial;j<i;++j) {
                    const auto& other=game.battle.soldiers[side*200+j];
                    assert(other.x!=s.x || other.y!=s.y);
                }
            }
            int markers=0;
            for(const auto& p:game.battle.projectiles) if(p.active && p.magic==36) {
                ++markers;
                assert(p.phase==1 && p.kind==expected && p.height>0 && p.target_offset_x>0);
                assert(!sg::visual::effect_visible(p) && !sg::visual::effect_frames_camera(p));
            }
            assert(markers==(expected>0));
            for(int tick=0;tick<50;++tick) game.battle_tick();
            for(const auto& p:game.battle.projectiles) assert(!(p.active && p.magic==36));
            assert(!game.battle.effect_remaining);
        }
    std::puts("PASS ambush: four tiers, both sides, missing/nearly-full/full armies; cavalry priority, real rear spawn, no duplicate cells, camera lifetime");
}
