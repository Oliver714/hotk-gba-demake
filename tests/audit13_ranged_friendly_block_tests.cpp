#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"

static bool run_side(int side) {
    siege(1,1);
    for(int s=0;s<2;++s) {
        auto& f=game.battle.sides[s];
        f.force=s==side ? 2 : 0;
        f.hp=1000;f.mp=0;f.action_ticks=60000;f.order=sg::Order::hold;
        f.soldiers=f.front=0;
    }
    constexpr int row=60;
    for(auto& soldier:game.battle.soldiers) { soldier={}; soldier.hp=0; }
    const int direction=side ? 1 : -1;
    const int shooter_x=side ? 1000 : 1200;
    auto& shooter=game.battle.soldiers[side*200];
    shooter.hp=10;shooter.side=uint8_t(side);shooter.index=0;
    shooter.x=shooter.previous_x=int16_t(shooter_x);shooter.y=shooter.previous_y=row;
    shooter.attack_direction=int8_t(direction);shooter.action_ticks=1;shooter.attack_ticks=1;
    auto& ally=game.battle.soldiers[side*200+1];
    ally.hp=10;ally.side=uint8_t(side);ally.index=1;
    ally.x=ally.previous_x=int16_t(shooter_x+direction*50);ally.y=ally.previous_y=row;
    auto& enemy=game.battle.soldiers[(1-side)*200];
    enemy.hp=10;enemy.side=uint8_t(1-side);enemy.index=0;
    enemy.x=enemy.previous_x=int16_t(shooter_x+direction*120);enemy.y=enemy.previous_y=row;
    game.battle.sides[side].soldiers=game.battle.sides[side].front=2;
    game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=1;

    game.battle_tick();
    for(const auto& projectile:game.battle.projectiles)
        if(projectile.active && projectile.magic==0 && projectile.owner_index==0) return true;
    return false;
}

int main() {
    int failures=0;
    for(int side=0;side<2;++side) failures+=!run_side(side);
    std::printf("%s: ranged acquisition skips an allied unit and reaches the enemy in both directions: %d failures /2\n",
                failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
