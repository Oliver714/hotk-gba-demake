#define SANGO_DUEL_AUDIT_NO_MAIN
#include "duel_fixture.h"
int main() {
    using namespace sg::original;
    static_assert(commander_hit_threshold(100,50)==80);
    static_assert(commander_hit_threshold(50,100)==40);
    static_assert(commander_hit_threshold(60,60)==60);
    static_assert(commander_attack_cooldown_updates(100)==3);
    static_assert(commander_attack_cooldown_updates(50)==9);
    static_assert(commander_attack_cooldown_updates(120)==0);
    static_assert(commander_attack_cooldown_updates(150)==0);
    for(int attacker=1;attacker<=150;++attacker) for(int defender=1;defender<=150;++defender) {
        assert(commander_hit_threshold(attacker,defender)>=40 && commander_hit_threshold(attacker,defender)<=80);
        if(attacker>=defender) assert(commander_hit_threshold(attacker,defender)>=commander_hit_threshold(defender,attacker));
        if(attacker<150) assert(commander_attack_cycle_updates(attacker+1)<=commander_attack_cycle_updates(attacker));
    }
    prepare(100,50,7);
    game.battle.sides[0].hp=game.battle.sides[1].hp=10000;
    int last[2]={-1,-1},attacks[2]={0,0};
    for(int tick=0;tick<600;++tick) {
        const int before[2]={game.battle.sides[0].hp,game.battle.sides[1].hp};
        game.battle_tick();
        for(int side=0;side<2;++side) {
            const auto& fighter=game.battle.sides[side];
            if(fighter.attack_ticks==21) {
                if(last[side]>=0) assert(tick-last[side]==commander_attack_cycle_updates(game.strength(fighter.general)));
                last[side]=tick; ++attacks[side];
            }
            if(fighter.hp!=before[side]) assert(game.battle.sides[1-side].attack_ticks==6);
        }
    }
    assert(attacks[0]>attacks[1]);
    const int pairs[][2]={{100,50},{100,60},{90,60},{80,60},{60,60}};
    for(const auto& pair:pairs) {
        int wins=0,draws=0,side_wins[2]={0,0};
        for(int side=0;side<2;++side) for(int seed=0;seed<128;++seed) {
            prepare(pair[side ? 1 : 0],pair[side ? 0 : 1],seed);
            while(game.battle.pending_winner<0 && game.battle.duel_tick<6000) game.battle_tick();
            wins+=game.battle.pending_winner==side;
            side_wins[side]+=game.battle.pending_winner==side;
            draws+=game.battle.pending_winner==2;
        }
        assert(!draws);
        if(pair[0]>pair[1]) assert(side_wins[0]>=115 && side_wins[1]>=115);
        else assert(side_wins[0]>=48 && side_wins[0]<=80); // Equal STR has no large side bias.
        std::printf("PASS pure duel STR%d vs%d stronger wins %d/256\n",pair[0],pair[1],wins);
    }
    std::puts("PASS: hit direction, clamping, STR cooldown, hit event and side-swap balance");
}
