#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"

static bool run_side(int side) {
    siege(1,1);
    for(int s=0;s<2;++s) {
        auto& f=game.battle.sides[s];
        f.hp=1000;f.action_ticks=60000;f.force=0;f.order=sg::Order::hold;
    }
    for(auto& soldier:game.battle.soldiers) { soldier={};soldier.hp=0; }
    const int direction=side ? 1 : -1;
    const int owner_x=side ? 800 : 1200;
    const int ally_x=side ? 1000 : 1000;
    auto& owner=game.battle.soldiers[side*200];
    owner.hp=10;owner.side=uint8_t(side);owner.index=0;
    owner.x=owner.previous_x=int16_t(owner_x);owner.y=owner.previous_y=60;owner.action_ticks=60000;
    auto& ally=game.battle.soldiers[side*200+1];
    ally.hp=10;ally.side=uint8_t(side);ally.index=1;
    ally.x=ally.previous_x=int16_t(ally_x);ally.y=ally.previous_y=60;ally.action_ticks=60000;
    auto& enemy=game.battle.soldiers[(1-side)*200];
    enemy.hp=10;enemy.side=uint8_t(1-side);enemy.index=0;
    enemy.x=enemy.previous_x=int16_t(side ? 1100 : 900);enemy.y=enemy.previous_y=60;enemy.action_ticks=60000;
    game.battle.sides[side].soldiers=game.battle.sides[side].front=2;
    game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=1;
    auto& arrow=game.battle.projectiles[0];
    arrow={};arrow.active=1;arrow.collision_enabled=1;arrow.target_side=int8_t(1-side);
    arrow.owner_index=0;arrow.x=int16_t(ally_x-direction*20);arrow.y=60;
    arrow.dx=int16_t(direction*4);arrow.target_offset_x=int16_t(owner_x);
    game.battle_tick();
    return arrow.active && arrow.collision_enabled && !arrow.remaining &&
           arrow.x==ally_x-direction*16;
}

int main() {
    int failures=0;
    for(int side=0;side<2;++side) failures+=!run_side(side);
    std::printf("%s: ordinary arrows pass through friendly units and keep moving in both directions: %d failures /2\n",
                failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
