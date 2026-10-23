#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
namespace sg { void demake_general_apply_hit(Game&,int,int); }
int main() {
    siege(1,1);int weak=0;
    for(int g=1;g<sg::general_count;++g) if(game.strength(g)<game.strength(weak)) weak=g;
    assert(game.strength(weak)<36);
    game.battle.sides[0].general=int16_t(weak);
    game.battle.sides[0].x=800;game.battle.sides[1].x=760;
    int heals=0,hits=0;
    for(unsigned seed=1;seed<=512;++seed) {
        game.campaign.random=seed;game.battle.sides[1].hp=60;
        sg::demake_general_apply_hit(game,0,-1);
        heals+=game.battle.sides[1].hp>60;hits+=game.battle.sides[1].hp<60;
    }
    std::printf("%s: STR %d normal attacks: healed enemy %d times, damaged %d / 512\n",heals?"FAIL":"PASS",game.strength(weak),heals,hits);
    return heals || !hits;
}
