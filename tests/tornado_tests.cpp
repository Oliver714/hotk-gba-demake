#define private public
#include "model.h"
#undef private
#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include "battle_camera.h"
#include <initializer_list>

int main() {
    const int ids[3]={56,67,76};
    for(int tier=1;tier<=3;++tier) for(int side=0;side<2;++side)
        for(int population: {0,1,100}) {
            prepare_skill_review(ids[tier-1],side,population);
            assert(sg::skill_by_id(ids[tier-1])->attack==8);
            sg::Projectile* tokens[3]{};
            int count=0;
            for(auto& p:game.battle.projectiles) if(p.active && p.magic==5) {
                assert(p.owner_index==count && p.remaining==24+18*count);
                assert(p.scatter==tier && p.phase==0);
                assert(p.x>=0 && p.x<sg::battlefield_width && p.y>=0 && p.y<=sg::battle_grid_max_y);
                if(p.kind<200)
                    for(int i=0;i<count;++i) assert(tokens[i]->kind!=p.kind);
                tokens[count++]=&p;
            }
            assert(count==tier);
            auto& force=game.battle.sides[1-side];
            const int own_count=game.battle.sides[side].soldiers;
            if(population) {
                auto& friendly=game.battle.soldiers[side*200];
                friendly.x=tokens[0]->x;friendly.y=tokens[0]->y;
            } else {
                // Explicit commander hit fixture exercises the fixed ATTACK=8.
                tokens[0]->x=force.x;tokens[0]->y=sg::battle_commander_y;
            }
            for(int tick=1;tick<=120;++tick) for(int i=0;i<count;++i) {
                auto& p=*tokens[i];
                if(!p.active) continue;
                const int old_count=force.soldiers,old_hp=force.hp;
                const int old_x=p.x,old_y=p.y;
                const bool activating=p.phase==0 && p.remaining==1;
                int hits=0;
                if(activating) for(int j=0;j<200;++j) {
                    const auto& s=game.battle.soldiers[(1-side)*200+j];
                    hits+=s.hp>0 && s.x>=p.x-50 && s.x<=p.x+50 && s.y>=p.y-50 && s.y<=p.y+50;
                }
                const bool general_hit=activating && force.x>=p.x-50 && force.x<=p.x+50 &&
                    sg::battle_commander_y>=p.y-50 && sg::battle_commander_y<=p.y+50;
                game.update_cyclone_projectile(p);
                assert(force.soldiers==old_count-hits);
                assert(force.hp==old_hp-(general_hit ? 8 : 0));
                assert(p.x==old_x && p.y==old_y); // no invented callback jitter
                if(activating) assert(tick==24+18*i && sg::visual::effect_visible(p));
                assert(game.battle.sides[side].soldiers==own_count);
                if(population) assert(game.battle.soldiers[side*200].hp==10);
            }
            for(int i=0;i<count;++i) assert(!tokens[i]->active);
        }
    std::puts("PASS tornado: all tiers, both sides, empty/single/full enemy; independent targets, staggered activation, enemy-only 100x100 hits, ATTACK=8 once, stable anchors, cleanup");
}
