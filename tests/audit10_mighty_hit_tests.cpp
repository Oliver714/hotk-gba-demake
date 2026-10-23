#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
static sg::Game initial;
int main() {
    siege(1,1);game.battle.sides[0].x=800;game.battle.sides[1].x=760;
    for(auto& f:game.battle.sides) {f.mp=0;f.action_ticks=60000;}
    game.battle.sides[0].action_ticks=0;
    game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
    initial=game;int inherited=0,started=0;
    for(unsigned seed=1;seed<=512;++seed) {
        game=initial;game.campaign.random=seed;game.battle_tick();
        const bool expected=game.battle.sides[0].ai_action_flags&sg::fighter_mighty_hit_flag;
        assert(game.battle.sides[0].attack_ticks>0);++started;
        game=initial;game.campaign.random=seed;game.battle.sides[0].ai_action_flags|=sg::fighter_mighty_hit_flag;
        game.battle_tick();
        inherited+=expected!=bool(game.battle.sides[0].ai_action_flags&sg::fighter_mighty_hit_flag);
    }
    std::printf("%s: previous mighty attack altered %d / %d subsequent attacks\n",inherited?"FAIL":"PASS",inherited,started);
    return inherited?1:0;
}
