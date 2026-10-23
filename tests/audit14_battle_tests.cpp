#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include "ranged_attack_fixture.h"

static void quiet() {
    siege(1,1);
    for(int side=0;side<2;++side) {
        auto& f=game.battle.sides[side];
        f.hp=1000;f.mp=0;f.action_ticks=60000;f.order=sg::Order::hold;
        f.ai_action_flags=sg::fighter_opening_finished_flag;
        f.soldiers=f.front=0;game.battle.ai_decision_countdown[side]=60000;
    }
    for(auto& s:game.battle.soldiers) s={};
}
static int friendly_general() {
    int failures=0;
    for(int side=0;side<2;++side) for(int mode=0;mode<2;++mode) {
        quiet();const int d=side?1:-1;
        auto& f=game.battle.sides[side];auto& enemy=game.battle.sides[1-side];
        f.force=2;f.soldiers=f.front=1;f.x=int16_t(1100+d*80);enemy.x=int16_t(1100+d*200);
        auto& s=game.battle.soldiers[side*200];s.hp=1;s.side=uint8_t(side);s.index=0;
        s.x=s.previous_x=1100;s.y=s.previous_y=sg::battle_commander_y;s.action_ticks=60000;
        if(mode==0) {
            failures+=!exercise_ranged_checks(game,side*200);
        } else {
            auto& p=game.battle.projectiles[0];p={};p.active=p.collision_enabled=1;
            p.x=int16_t(f.x-d*4);p.y=sg::battle_commander_y;p.dx=int16_t(d*4);
            p.target_side=int8_t(1-side);p.target_offset_x=s.x;
            game.battle_tick();failures+=!p.collision_enabled || p.remaining!=0;
        }
    }
    std::printf("%s friendly general: acquisition and arrow flight, both sides (%d failures /4)\n",failures?"FAIL":"PASS",failures);
    return failures;
}
static int lethal_escape() {
    int failures=0;
    for(int side=0;side<2;++side) {
        quiet();auto& f=game.battle.sides[side];auto& enemy=game.battle.sides[1-side];
        f.order=sg::Order::retreat;f.x=int16_t(side?1:sg::battlefield_width-2);
        enemy.x=1000;enemy.hp=1;game.battle.tick=2;
        game.battle.magic_callback_damage=12;
        auto& p=game.battle.projectiles[0];p={};p.active=p.collision_enabled=1;
        p.magic=1;p.phase=1;p.source_script=2630;p.target_side=int8_t(1-side);
        p.x=1000;p.y=sg::battle_commander_y;p.dx=side?1:-1;
        game.battle_tick();assert(enemy.hp==0);
        failures+=game.battle.pending_winner!=side || bool(f.ai_action_flags&sg::fighter_escaped_flag);
        quiet();auto& escape=game.battle.sides[side];
        escape.order=sg::Order::retreat;escape.x=int16_t(side?1:sg::battlefield_width-2);game.battle.tick=2;
        game.battle_tick();
        failures+=game.battle.pending_winner!=1-side || !(escape.ai_action_flags&sg::fighter_escaped_flag);
    }
    std::printf("%s lethal/escape: lethal hit takes priority; live-opponent escapes still work (%d failures /4)\n",failures?"FAIL":"PASS",failures);
    return failures;
}
static int stop_interpolation() {
    int failures=0;
    for(int side=0;side<2;++side) for(int same=0;same<2;++same) {
        quiet();auto& f=game.battle.sides[side];f.soldiers=f.front=1;
        f.order=same?sg::Order::hold:sg::Order::advance;
        auto& s=game.battle.soldiers[side*200];s.hp=1;s.side=uint8_t(side);
        s.x=s.previous_x=1100;s.y=s.previous_y=60;s.action_ticks=60000;
        s.move_ticks=16;s.move_dx=side?1:-1;s.move_target_x=int16_t(1100+s.move_dx*20);s.move_target_y=60;
        if(same) {s.move_dx=0;s.move_dy=1;s.move_target_x=1100;s.move_target_y=75;}
        assert(game.issue_battle_command(side,sg::BattleCommand::hold));game.battle_tick();
        failures+=s.x!=1100 || s.y!=60 || s.move_ticks!=0;
        if(same) failures+=f.action_ticks!=59999;
    }
    std::printf("%s hold: stop pending steps including formation movement under hold (%d failures /4)\n",failures?"FAIL":"PASS",failures);
    return failures;
}
static int ranged_current_cell() {
    int failures=0;
    const int forces[]={2,4,5};
    for(int side=0;side<2;++side) for(int force:forces) for(int control=0;control<4;++control) {
        quiet();auto& f=game.battle.sides[side];f.force=uint8_t(force);f.soldiers=f.front=1;
        auto& shooter=game.battle.soldiers[side*200];shooter.hp=1;shooter.side=uint8_t(side);
        shooter.x=shooter.previous_x=1000;shooter.y=shooter.previous_y=60;shooter.action_ticks=1;
        auto& enemy=game.battle.soldiers[(1-side)*200];enemy.hp=control==3?0:1;enemy.side=uint8_t(1-side);
        enemy.x=enemy.previous_x=int16_t(control==0?1000:1008);
        enemy.y=enemy.previous_y=int16_t(control==2?90:60);enemy.action_ticks=60000;
        game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=enemy.hp;
        game.battle_tick();failures+=bool(shooter.attack_ticks)!=(control<2);
    }
    std::printf("%s ranged current cell: include overlapping enemies, exclude other rows/dead (%d failures /24)\n",failures?"FAIL":"PASS",failures);
    return failures;
}
static int formation_attack() {
    int failures=0;
    for(int side=0;side<2;++side) for(int attacking=0;attacking<2;++attacking) {
        quiet();auto& f=game.battle.sides[side];f.soldiers=f.front=1;
        auto& s=game.battle.soldiers[side*200];s.hp=1;s.side=uint8_t(side);
        s.x=s.previous_x=1000;s.y=s.previous_y=60;s.action_ticks=60000;
        s.attack_ticks=attacking?8:0;
        assert(game.issue_battle_command(side,sg::BattleCommand::disperse));
        for(int tick=0;tick<(attacking?8:1);++tick) game.battle_tick();
        if(attacking) {
            failures+=s.move_ticks!=32 || s.x!=1000 || s.y!=60 || s.attack_ticks!=0;
            game.battle_tick();failures+=s.move_ticks!=31;
        } else failures+=s.move_ticks!=31;
    }
    std::printf("%s formation: finish an active attack before interpolation; idle actors move immediately (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures;
}
int main() {
    int failures=friendly_general()+lethal_escape()+stop_interpolation()+ranged_current_cell()+formation_attack();
    return failures?1:0;
}
