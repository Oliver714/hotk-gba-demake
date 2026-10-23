#include "model.h"
#include <cassert>
#include <cstddef>
#include <cstdio>

namespace {
sg::Game game;
int commander_x[2]={2000,200};
void fixture() {
    game.new_game(1,1);
    game.battle={};
    game.campaign.phase=sg::Phase::battle;
    auto& battle=game.battle;
    battle.army=0; battle.opponent_army=1; battle.city=-1; battle.field_battle=1;
    battle.pending_winner=-1;
    game.campaign.armies[0].king=1;
    game.campaign.armies[1].king=0;
    for(int side=0;side<2;++side) {
        auto& fighter=battle.sides[side];
        fighter.general=int16_t(side);
        game.campaign.officers[side].king=int8_t(side ? 0 : 1);
        fighter.hp=int16_t(game.max_hp(side)); fighter.initial_hp=fighter.hp;
        fighter.x=fighter.previous_x=fighter.deployment_x=int16_t(commander_x[side]);
        fighter.order=sg::Order::hold;
        fighter.attack_direction=int8_t(side ? 1 : -1);
        fighter.soldier_hit_threshold=300;
        fighter.action_ticks=60000;
        battle.ai_decision_countdown[side]=60000;
    }
}
sg::Soldier& add(int side,int index,int x,int y) {
    auto& soldier=game.battle.soldiers[side*200+index];
    soldier={};
    soldier.x=soldier.previous_x=soldier.move_target_x=int16_t(x);
    soldier.y=soldier.previous_y=soldier.move_target_y=int16_t(y);
    soldier.hp=1; soldier.side=uint8_t(side); soldier.index=uint8_t(index); soldier.formation_column=1;
    if(side) soldier.action_ticks=60000;
    ++game.battle.sides[side].soldiers; ++game.battle.sides[side].front;
    return soldier;
}
void tick() {
    // Isolate soldier decisions from mounted commanders' movement and attacks.
    for(int side=0;side<2;++side) game.battle.sides[side].x=int16_t(commander_x[side]);
    game.battle_tick();
}
void run(int count) { while(count-- && game.battle.pending_winner<0) tick(); }
void charge() { assert(game.issue_battle_order(0,sg::Order::advance)); }
void save_fixture(const char* path) {
    auto* file=std::fopen(path,"wb"); assert(file);
    const auto bytes=offsetof(sg::Game,battle_report);
    assert(std::fwrite(&game,1,bytes,file)==bytes); std::fclose(file);
}
void contact(int enemy_x,int own_y,int enemy_y) {
    fixture();
    add(0,0,1040,own_y); add(1,0,enemy_x,enemy_y);
    // demake's block roll may reject the first swing. Exercise several complete
    // 32-update attack cycles rather than assuming the old guaranteed hit.
    charge();
    for(int i=0;i<160 && game.battle.soldiers[200].hp;++i) tick();
    assert(game.battle.soldiers[200].hp==0);
    assert(game.battle.soldiers[0].x==1040);
    assert(game.battle.soldiers[0].move_ticks==0);
}
}
int main(int argc,char** argv) {
    // Direction and attack row use logical cells, not fixed facing or exact y.
    contact(1000,180,180);
    contact(1080,180,180);
    assert(game.battle.soldiers[0].attack_direction==1);
    assert(game.battle.soldier_visual[0].facing==1);
    contact(1000,180,188);

    fixture(); add(0,0,1040,180); add(1,0,1000,210);
    charge(); tick();
    assert(!game.battle.soldiers[0].attack_ticks && game.battle.soldiers[200].hp==1);

    // A friendly first cell blocks melee acquisition, even if an enemy is behind.
    fixture(); add(0,0,1040,180); add(0,1,1000,180).action_ticks=60000; add(1,0,960,180);
    charge(); game.battle.soldiers[1].action_ticks=60000; tick();
    assert(!game.battle.soldiers[0].attack_ticks && game.battle.soldiers[200].hp==1);

    // Approach, stop at an adjacent cell, and finish the swing before moving.
    fixture(); add(0,0,1120,180); add(1,0,1000,180); charge();
    for(int i=0;i<160 && game.battle.soldiers[200].hp;++i) tick();
    assert(game.battle.soldiers[200].hp==0 && game.battle.soldiers[0].x==1040);

    // Remaining ranks take priority over the adjacent enemy commander. Chase
    // a soldier behind and off the starting lane, then switch to the commander.
    commander_x[1]=1000;
    fixture(); add(0,0,1040,180); add(1,0,1240,270); charge();
    const int general_hp=game.battle.sides[1].hp;
    for(int i=0;i<500 && game.battle.soldiers[200].hp;++i) {
        tick(); assert(game.battle.sides[1].hp==general_hp);
    }
    assert(game.battle.soldiers[200].hp==0);
    run(500);
    assert(game.battle.sides[1].hp<general_hp);

    // Losing one target does not stop the search for the next surviving rank.
    commander_x[1]=200;
    fixture(); add(0,0,1040,180); add(1,0,1000,180); add(1,1,1240,240); charge();
    for(int i=0;i<650 && game.battle.sides[1].soldiers;++i) tick();
    assert(!game.battle.soldiers[200].hp && !game.battle.soldiers[201].hp);

    // Ranged fire follows the selected target direction when facing reverses.
    // Keep both generals off this lane so only the intended soldier can be acquired.
    fixture(); add(0,0,1040,60); add(1,0,1200,60); game.battle.sides[0].force=2; charge();
    // Distant ranged acquisition is randomized; cadence is tested separately.
    // Observe the first shot over a bounded window rather than assuming tick17.
    bool rightward=false;
    for(int i=0;i<360 && !rightward;++i) {
        tick();
        for(const auto& projectile:game.battle.projectiles)
            if(projectile.active && projectile.dx>0) rightward=true;
    }
    assert(rightward);
    for(int i=0;i<3600 && game.battle.soldiers[200].hp;++i) tick();
    assert(!game.battle.soldiers[200].hp);

    fixture(); add(0,0,1040,180); add(1,0,1000,180);
    assert(game.issue_battle_order(0,sg::Order::retreat)); run(60);
    assert(game.battle.soldiers[200].hp==1 && !game.battle.soldiers[0].attack_ticks);

    // Whole ranks must continue acquiring survivors as front cells clear.
    fixture();
    for(int i=0;i<100;++i) {
        add(0,i,1440+(i/14)*40,(i%14)*30);
        add(1,i,560+(i/14)*40,(i%14)*30);
    }
    charge();
    for(int i=0;i<3000 && game.battle.sides[1].soldiers;++i) tick();
    assert(game.battle.sides[1].soldiers==0);

    if(argc>1) {
        commander_x[0]=1120; commander_x[1]=840;
        fixture(); add(0,0,1040,180).action_ticks=60000; add(1,0,1000,188);
        save_fixture(argv[1]);
    }
    if(argc>2) {
        commander_x[0]=1120; commander_x[1]=840;
        fixture(); add(0,0,1040,180).action_ticks=60000; add(1,0,1240,270);
        // Preserve the mounted actor's long action timer when issuing charge;
        // direction zero prevents its one forced animation from dealing damage.
        // Only the soldier can kill the rank or injure the enemy commander.
        game.battle.sides[0].attack_ticks=255;
        game.battle.sides[0].attack_direction=0;
        save_fixture(argv[2]);
    }
    std::puts("PASS: soldier contact, reverse facing, cell rows, blocking, pursuit, retargeting, soldier-first priority, commander fallback, ranged direction and retreat");
}
