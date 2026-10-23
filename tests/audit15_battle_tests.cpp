#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include <initializer_list>
static void quiet15() {
    siege(1,1);
    for(int side=0;side<2;++side) {
        auto& f=game.battle.sides[side];f.hp=1000;f.mp=0;f.action_ticks=60000;
        f.order=sg::Order::hold;f.soldiers=f.front=0;
        f.ai_action_flags=sg::fighter_opening_finished_flag;game.battle.ai_decision_countdown[side]=60000;
    }
    for(auto& s:game.battle.soldiers) s={};
}
static void queued_archer15(int side,int force=2,int future_only=0) {
    quiet15();const int d=side?1:-1;
    auto& f=game.battle.sides[side];f.force=uint8_t(force);f.soldiers=f.front=1;
    auto& s=game.battle.soldiers[side*200];s.hp=1;s.side=uint8_t(side);
    s.x=s.previous_x=1000;s.y=s.previous_y=60;s.action_ticks=100;s.attack_ticks=1;s.attack_direction=int8_t(d);
    auto& enemy=game.battle.soldiers[(1-side)*200];enemy.hp=1;enemy.side=uint8_t(1-side);
    enemy.x=enemy.previous_x=int16_t(1000+d*120);enemy.y=enemy.previous_y=int16_t(future_only?30:60);enemy.action_ticks=60000;
    game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=1;
    assert(game.issue_battle_command(side,sg::BattleCommand::disperse));
}
static void flame15(int side,int control=0) {
    quiet15();const int target=1-side;game.battle.sides[target].x=1000;
    auto& victim=game.battle.soldiers[target*200];victim.hp=control==2?0:1;victim.side=uint8_t(target);
    victim.x=victim.previous_x=1000;victim.y=victim.previous_y=sg::battle_commander_y;victim.action_ticks=60000;
    game.battle.sides[target].soldiers=game.battle.sides[target].front=victim.hp;
    game.battle.magic_callback_damage=12;
    if(control==0) {
        auto& hit=game.battle.projectiles[0];hit={};hit.active=1;hit.magic=10;hit.phase=1;hit.remaining=1;
        hit.x=1000;hit.y=sg::battle_commander_y;hit.target_side=int8_t(target);hit.source_script=2657;
    }
    auto& fire=game.battle.projectiles[1];fire={};fire.active=fire.collision_enabled=1;
    fire.magic=30;fire.phase=1;fire.source_script=2622;fire.target_side=int8_t(target);
    fire.x=1000;fire.y=sg::battle_commander_y;fire.scatter=128;fire.remaining=1;
}
static void retreat_block15(int side,int control=0) {
    quiet15();const int d=side?1:-1;auto& f=game.battle.sides[side];f.x=1000;f.order=sg::Order::retreat;
    game.battle.tick=2;auto& s=game.battle.soldiers[side*200];s.hp=control==3?0:1;s.side=uint8_t(side);
    s.x=s.previous_x=int16_t(1000+(control==1?d:-d)*40);
    s.y=s.previous_y=int16_t(sg::battle_commander_y+(control==2?30:0));s.action_ticks=60000;
    f.soldiers=f.front=s.hp;
}
int main() {
    int failures=0;
    for(int side=0;side<2;++side) for(int force:{2,4,5}) for(int future=0;future<2;++future) {
        queued_archer15(side,force,future);game.battle_tick();
        bool fired=false;for(const auto& p:game.battle.projectiles) fired|=p.active && p.magic==0;
        failures+=fired!=!future;
    }
    std::printf("%s queued ranged attack uses current row, not reserved destination: %d failures /12\n",failures?"FAIL":"PASS",failures);
    int flame_failures=0;
    for(int side=0;side<2;++side) for(int control=0;control<3;++control) {
        flame15(side,control);game.battle_tick();
        flame_failures+=(game.battle.sides[1-side].hp<1000)!=(control!=1);
    }
    std::printf("%s dead cell head cannot shield a general; live blockers remain: %d failures /6\n",flame_failures?"FAIL":"PASS",flame_failures);
    int blocked_failures=0;
    for(int side=0;side<2;++side) for(int control=0;control<4;++control) {
        retreat_block15(side,control);game.battle_tick();
        blocked_failures+=(game.battle.sides[side].x==1000)!=(control==0);
    }
    std::printf("%s retreat respects allied rear-path occupancy, with forward/off-row/dead controls: %d failures /8\n",blocked_failures?"FAIL":"PASS",blocked_failures);
    return failures+flame_failures+blocked_failures?1:0;
}
