#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstring>
int main() {
    for(int side=0;side<2;++side) {
        siege(1,1);
        auto& fighter=game.battle.sides[side];
        game.campaign.officers[fighter.general].learned[0]=55;
        fighter.mp=999;fighter.charge=sg::original::cast_charge_max;
        fighter.order=sg::Order::retreat;
        const auto before=game;
        if(game.cast_skill(side,0)) {
            std::printf("FAIL side%d cast sunset bow while escaping\n",side);return 1;
        }
        assert(std::memcmp(&before.campaign,&game.campaign,sizeof(game.campaign))==0);
        assert(std::memcmp(&before.battle,&game.battle,sizeof(game.battle))==0);
        fighter.order=sg::Order::hold;
        assert(game.cast_skill(side,0));
    }
    // Retreat must not cancel the remaining children of an ultimate that
    // already started legitimately. Its supervisor owns that cast lifecycle.
    for(int id=0;id<sg::skill_count;++id) if(sg::skill_defs[id].script==2663 || sg::skill_defs[id].script==2664) {
        siege(1,1);
        auto& f=game.battle.sides[0];f.hp=10000;f.mp=999;
        game.battle.sides[1].hp=10000;
        f.charge=sg::original::cast_charge_max;
        game.campaign.officers[f.general].learned[0]=uint8_t(sg::skill_defs[id].id);
        for(auto& fighter:game.battle.sides) fighter.action_ticks=60000;
        assert(game.cast_skill(0,0));
        f.order=sg::Order::retreat;
        bool reached_last=false;
        for(int tick=0;tick<1000;++tick) {
            // Hold still to isolate the child supervisor, not escape victory.
            f.action_ticks=60000;game.battle_tick();
            reached_last|=game.battle.magic_composite_stage==2;
        }
        assert(reached_last && !game.battle.magic_composite_kind);
    }
    std::puts("PASS: both sides cannot start a skill while retreating; normal casts unaffected");
}
