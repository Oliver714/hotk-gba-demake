#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
namespace sg { void demake_general_ai_tick(Game&,int,bool); }
static sg::Game initial;
static unsigned decision(unsigned seed,bool wrapped) {
    game=initial;game.campaign.random=seed;game.battle.ai_decision_countdown[1]=1;
    game.battle.tick=uint16_t(wrapped?10:1000);
    game.battle.sides[1].ai_last_action_tick=uint16_t(wrapped?65500:954);
    sg::demake_general_ai_tick(game,1,false);
    return game.battle.sides[1].ai_action_flags;
}
int main() {
    siege(1,1);
    int weak=0,strong=0;
    for(int g=1;g<sg::general_count;++g) {
        if(game.strength(g)<game.strength(weak)) weak=g;
        if(game.strength(g)>game.strength(strong)) strong=g;
    }
    game.battle.sides[0].general=int16_t(weak);game.battle.sides[1].general=int16_t(strong);
    for(auto& f:game.battle.sides) {f.hp=f.initial_hp=80;f.mp=0;f.charge=0;}
    game.battle.sides[1].soldiers=50;game.battle.sides[0].soldiers=10;
    game.battle.sides[1].ai_action_flags=1|sg::fighter_opening_finished_flag;
    initial=game;int mismatches=0;
    for(unsigned seed=1;seed<=1024;++seed) mismatches+=decision(seed,false)!=decision(seed,true);
    std::printf("%s: same 46-tick AI cooldown across clock wrap: %d / 1024 mismatches\n",mismatches?"FAIL":"PASS",mismatches);
    return mismatches?1:0;
}
