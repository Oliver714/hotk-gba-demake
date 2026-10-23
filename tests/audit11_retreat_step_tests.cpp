#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
int main() {
    int reversals=0;
    for(int side=0;side<2;++side) for(int back=0;back<2;++back) {
        siege(1,1);
        for(auto& f:game.battle.sides) {f.action_ticks=60000;f.mp=0;}
        game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
        auto& f=game.battle.sides[side];f.soldiers=f.front=1;
        auto& s=game.battle.soldiers[side*200];s={};s.hp=1;s.side=uint8_t(side);s.index=0;
        const int forward=side?1:-1;s.x=800;s.y=300;s.move_target_x=int16_t(800+forward*40);
        s.move_target_y=300;s.move_dx=int8_t(forward);s.move_ticks=32;s.action_ticks=60000;
        assert(game.issue_battle_command(side,back?sg::BattleCommand::front_back:sg::BattleCommand::retreat));
        for(int tick=0;tick<40;++tick) {
            const int old=s.x;game.battle_tick();
            if((int(s.x)-old)*forward>0) ++reversals;
        }
    }
    std::printf("%s: retreat/backstep must not continue old forward interpolation: %d forward jumps\n",reversals?"FAIL":"PASS",reversals);
    return reversals?1:0;
}
