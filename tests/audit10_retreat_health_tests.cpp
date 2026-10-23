#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
namespace sg { void demake_general_ai_tick(Game&,int,bool); }
static sg::Game initial;
int main() {
    siege(1,1);
    game.campaign.officers[game.battle.sides[1].general].hp=5;
    game.start_duel();
    auto& own=game.battle.sides[1];own.ai_action_flags=sg::fighter_opening_checked_flag|sg::fighter_opening_finished_flag;
    own.order=sg::Order::general;own.mp=0;own.charge=0;
    game.battle.sides[0].hp=80;game.battle.sides[0].order=sg::Order::hold;
    assert(own.hp==15);initial=game;
    int unchanged_retreats=0,damaged_retreats=0;
    for(unsigned seed=1;seed<=512;++seed) for(int damaged=0;damaged<2;++damaged) {
        game=initial;game.campaign.random=seed;game.battle.ai_decision_countdown[1]=1;
        if(damaged) --game.battle.sides[1].hp;
        sg::demake_general_ai_tick(game,1,false);
        if(game.battle.sides[1].order==sg::Order::retreat) {
            if(damaged) ++damaged_retreats;else ++unchanged_retreats;
        }
    }
    std::printf("%s: unchanged wounded re-entry retreats=%d; newly damaged retreats=%d / 512\n",!unchanged_retreats && damaged_retreats?"PASS":"FAIL",unchanged_retreats,damaged_retreats);
    return unchanged_retreats || !damaged_retreats;
}
