#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
int main() {
    int missed=0;
    for(int side=0;side<2;++side) {
        siege(1,1);
        for(auto& f:game.battle.sides) {f.mp=0;f.action_ticks=60000;}
        game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
        auto& f=game.battle.sides[side];auto& enemy=game.battle.sides[1-side];
        f.x=800;enemy.x=int16_t(side?750:850);
        f.order=sg::Order::general;f.move_command8=1;
        assert(game.issue_battle_command(side,sg::BattleCommand::hold));
        game.battle_tick();
        missed+=!f.attack_ticks;
        if(f.attack_ticks) assert(f.attack_direction==(side?-1:1));
    }
    std::printf("%s: hold after advance must reacquire an in-range rear general: %d misses /2\n",missed?"FAIL":"PASS",missed);
    return missed;
}
