#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstring>
namespace sg { void demake_general_ai_tick(Game&,int,bool); }
static sg::Game initial;
static void decide(int own_force,int enemy_force,unsigned seed) {
    game=initial;game.campaign.random=seed;
    game.battle.sides[1].force=uint8_t(own_force);
    game.battle.sides[0].force=uint8_t(enemy_force);
    game.battle.ai_decision_countdown[1]=1;
    sg::demake_general_ai_tick(game,1,false);
}
int main() {
    siege(1,1);
    for(auto& f:game.battle.sides) {
        f.hp=f.initial_hp=80;f.soldiers=50;f.mp=0;f.charge=0;
        f.ai_action_flags=sg::fighter_opening_finished_flag;
    }
    initial=game;
    int mismatches=0;
    for(unsigned seed=1;seed<=512;++seed) for(int own=0;own<2;++own) {
        decide(own ? 2 : 0,own ? 0 : 2,seed);
        const auto expected=game.battle.sides[1];
        decide(own ? 4 : 0,own ? 0 : 4,seed);
        const auto& actual=game.battle.sides[1];
        if(expected.ai_action_flags!=actual.ai_action_flags ||
           std::memcmp(expected.ai_command_slots,actual.ai_command_slots,sizeof(actual.ai_command_slots))) ++mismatches;
    }
    std::printf("%s: same ranged class must use same tactical weights; %d mismatches / 1024 seeded decisions\n",mismatches?"FAIL":"PASS",mismatches);
    return mismatches?1:0;
}
