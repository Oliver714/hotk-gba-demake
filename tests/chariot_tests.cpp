#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstddef>
#include <cstdlib>
#include <initializer_list>

void prepare_chariot(int script,int side) {
    siege(1,1);
    for(int s=0;s<2;++s) {
        auto& f=game.battle.sides[s];
        f.hp=10000; f.mp=1000; f.charge=120000;
        f.x=f.previous_x=s ? 200 : 2100;
        f.order=sg::Order::hold; f.soldiers=f.front=s==side ? 0 : 200;
        f.action_ticks=60000;
        game.campaign.officers[f.general].king=game.campaign.player;
        game.battle.ai_decision_countdown[s]=60000;
    }
    constexpr int lanes[8]={45,75,105,135,315,345,375,405};
    for(int s=0;s<2;++s) for(int i=0;i<200;++i) {
        auto& soldier=game.battle.soldiers[s*200+i]; soldier={};
        soldier.hp=s==side ? 0 : 10; soldier.side=uint8_t(s); soldier.index=uint8_t(i);
        soldier.x=soldier.previous_x=int16_t(800+(i/8)*30);
        soldier.y=soldier.previous_y=int16_t(lanes[i%8]);
        soldier.action_ticks=60000;
    }
    int index=0;
    while(index<sg::skill_count && sg::skill_defs[index].script!=script) ++index;
    assert(index<sg::skill_count);
    game.campaign.officers[game.battle.sides[side].general].learned[0]=sg::skill_defs[index].id;
    assert(game.cast_skill(side,0));
}

#ifndef SANGO_CHARIOT_NO_MAIN
int main() {
    for(int script : {2627,2628}) for(int side=0;side<2;++side) {
        prepare_chariot(script,side);
        bool saw_cart=false;
        for(int tick=0;tick<500;++tick) {
            game.battle_tick();
            for(const auto& p:game.battle.projectiles) if(p.active) {
                assert(!(p.magic==21 && p.source_script==script));
                saw_cart |= p.magic==20 && p.phase==1;
            }
        }
        assert(saw_cart && game.battle.sides[1-side].soldiers<200);
        assert(game.battle.sides[0].hp==10000 && game.battle.sides[1].hp==10000);
        for(const auto& p:game.battle.projectiles) assert(!p.active);
        std::printf("PASS script%d side%d: %d soldiers hit; carts ended without explosions or general damage\n",
                    script,side,200-game.battle.sides[1-side].soldiers);
    }
}
#endif
