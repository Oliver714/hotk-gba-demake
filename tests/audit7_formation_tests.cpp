#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
int main() {
    for(int side=0;side<2;++side) for(int interrupted=1;interrupted<32;++interrupted) {
    siege(1,1);
    auto& fighter=game.battle.sides[side];
    fighter.soldiers=fighter.front=1;
    fighter.mp=0;
    auto& soldier=game.battle.soldiers[side*200];soldier={};
    soldier.hp=10;soldier.side=uint8_t(side);soldier.x=soldier.previous_x=1200;
    soldier.y=soldier.previous_y=90;
    soldier.action_ticks=1000;
    assert(game.issue_battle_command(side,sg::BattleCommand::disperse));
    for(int i=0;i<interrupted;++i) game.battle_tick();
    const auto previous_target=soldier.move_target_y;
    const auto remaining=soldier.move_ticks;
    const bool accepted=game.issue_battle_command(side,sg::BattleCommand::disperse);
    if(accepted || soldier.move_target_y!=previous_target || soldier.move_ticks!=remaining) {
        std::printf("FAIL: repeated command retargets in-flight soldier from y%d to y%d (remaining %d -> %d)\n",
            previous_target,soldier.move_target_y,remaining,soldier.move_ticks);return 1;
    }
    assert(!game.issue_battle_command(side,sg::BattleCommand::fall_in));
    assert(soldier.move_target_y==previous_target && soldier.move_ticks==remaining);
    for(int i=0;i<40;++i) game.battle_tick();
    assert(soldier.y==previous_target && soldier.y%sg::battle_grid_row_height==0);
    assert(game.issue_battle_command(side,sg::BattleCommand::fall_in));
    }
    std::puts("PASS: 62 formation transitions, both commands and sides; interpolation and grid rows preserved");
}
