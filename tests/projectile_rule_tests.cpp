// Projectile rules reported by the player:
//   1. a missed shot must not reverse and fly back at its owner;
//   2. the M011 crescent slash must cut down soldiers along its path;
//   3. the crescent slash and the bow shots must end when they strike the
//      opposing general instead of flying on past.
#include "model.h"
#include "original_rules.h"
#include "battle_camera.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace {
constexpr int side_slots = 200;
// Row 10 (y 285..314) is far from both commanders, which sit on row 6, so a
// soldier parked there cannot be reached by ordinary melee in a single tick.
constexpr int test_x = 400, test_y = 300;
sg::Game game;

void siege() {
    game.new_game(1,1);
    for(auto& city : game.campaign.cities) { city.king=-1; city.major=-1; }
    for(auto& officer : game.campaign.officers) {
        officer.status=sg::Status::absent;
        officer.king=-1; officer.city=-1; officer.army=-1;
    }
    int neighbors[sg::city_count];
    assert(game.neighbors(5,neighbors)>0);
    const int target=neighbors[0];
    game.campaign.cities[5].king=1;
    game.campaign.cities[target].king=0;
    game.campaign.cities[target].major=2;
    for(int side=0;side<2;++side) {
        auto& officer=game.campaign.officers[side*2];
        officer.status=sg::Status::employed;
        officer.king=side ? 0 : 1;
        officer.city=side ? target : 5;
        officer.hp=60; officer.mp=0; officer.front=officer.soldiers=0;
    }
    game.campaign.phase=sg::Phase::world;
    int attackers[1]={0};
    assert(game.send_army(5,target,attackers,1));
    game.start_battle(0);
    assert(game.campaign.phase==sg::Phase::battle);
    game.start_duel();
    game.battle.paused=0;
    game.battle.row_setup_active=0;
    game.battle.pending_winner=-1;
}

void clear_soldiers() {
    for(auto& s : game.battle.soldiers) s.hp=0;
    game.battle.sides[0].soldiers=game.battle.sides[1].soldiers=0;
    game.battle.sides[0].front=game.battle.sides[1].front=0;
}

void place_soldier(int side,int x,int y) {
    auto& s=game.battle.soldiers[side*side_slots];
    // index 1, not 0: a projectile spawned with owner_index 0 would treat a
    // slot-0 soldier as its own shooter and skip it.
    s.hp=1; s.side=uint8_t(side); s.index=1; s.rear=0; s.formation_column=0;
    s.x=int16_t(x); s.y=int16_t(y);
    s.previous_x=s.x; s.previous_y=s.y;
    s.move_target_x=s.x; s.move_target_y=s.y;
    s.move_dx=s.move_dy=0; s.move_ticks=0; s.move_command8=0;
    s.attack_ticks=0; s.action_ticks=0; s.action_delay=0;
    ++game.battle.sides[side].soldiers;
    ++game.battle.sides[side].front;
}

sg::Projectile& spawn(int script,int target_side,int x,int y,int dx,int magic) {
    for(auto& p : game.battle.projectiles) if(!p.active) {
        p={};
        p.active=1; p.x=int16_t(x); p.y=int16_t(y); p.dx=int16_t(dx);
        p.source_script=int16_t(script); p.target_side=int8_t(target_side);
        p.collision_enabled=1; p.magic=uint8_t(magic); p.phase=1;
        return p;
    }
    assert(!"projectile pool full");
    return game.battle.projectiles[0];
}

// Only the travelling shot counts: striking a general also spawns a separate
// magic=22 burst visual carrying the same source_script.
bool shot_alive(int script) {
    for(const auto& p : game.battle.projectiles)
        if(p.active && p.magic==1 && p.source_script==script) return true;
    return false;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // --- 1. A friendly unit does not absorb an ordinary shot. ---
    {
        siege();
        clear_soldiers();
        // The projectile should pass through its own side without entering
        // the miss animation; demake only dispatches collision for enemies.
        place_soldier(0,test_x,test_y);
        auto& arrow=spawn(0,1,test_x-4,test_y,6,0);
        const int heading=arrow.dx, start_x=arrow.x;
        game.battle_tick();
        assert(arrow.x>start_x);
        assert(arrow.dx==heading);
        assert(arrow.collision_enabled==1 && arrow.remaining==0);
        std::printf("PASS ordinary shot passed its allied unit and kept heading dx=%d\n",heading);
    }

    // --- 2. The M011 crescent cuts soldiers; the bow shots do not. ---
    for(int script : {2630,2631,2632}) {
        siege();
        clear_soldiers();
        place_soldier(1,test_x,test_y);
        spawn(script,1,test_x-4,test_y,20,1);
        game.battle_tick();
        assert(game.battle.soldiers[side_slots].hp<=0);
        assert(game.battle.sides[1].soldiers==0);
        assert(sg::original::magic_projectile_cuts_soldiers(script));
        std::printf("PASS crescent %d killed the swept soldier\n",script);
    }
    for(int script : {2633,2634,2660}) {
        siege();
        clear_soldiers();
        place_soldier(1,test_x,test_y);
        spawn(script,1,test_x-4,test_y,20,1);
        game.battle_tick();
        assert(game.battle.soldiers[side_slots].hp>0);
        assert(game.battle.sides[1].soldiers==1);
        assert(!sg::original::magic_projectile_cuts_soldiers(script));
        std::printf("PASS bow shot %d passes soldiers\n",script);
    }

    // --- 3. Striking the general ends the travelling shot. ---
    for(int script : {2630,2632,2633,2660}) {
        siege();
        clear_soldiers();
        game.battle.sides[1].hp=100;
        game.battle.sides[1].x=int16_t(test_x);
        auto& shot=spawn(script,1,test_x-4,sg::battle_commander_y,20,1);
        game.battle_tick();
        assert(shot.collision_enabled==0);
        assert(shot.remaining==1);
        game.battle_tick();
        assert(shot.remaining==0 && shot.active==0);
        assert(!shot_alive(script));
        std::printf("PASS %d ended after striking the general\n",script);
    }

    // Direct magic shots must not miss a general just because soldiers
    // share his grid cell (the dense close-combat case).
    for(int script : {2630,2631,2632,2633,2634,2660,2645,2646,2647}) for(int target_side : {0,1}) {
        siege();
        clear_soldiers();
        game.battle.sides[1-target_side].x=target_side ? 1000 : 100;
        game.battle.sides[target_side].hp=100;
        game.battle.sides[target_side].x=test_x;
        place_soldier(target_side,test_x,sg::battle_commander_y);
        const auto first=game.battle.soldiers[target_side*side_slots];
        for(int i=1;i<20;++i) {
            auto& s=game.battle.soldiers[target_side*side_slots+i];
            s=first;s.index=uint8_t(i+1);s.action_ticks=200;
        }
        game.battle.sides[target_side].soldiers=game.battle.sides[target_side].front=20;
        game.battle.magic_callback_damage=uint8_t(sg::original::magic_general_hit_bonus(script));
        auto& projectile=spawn(script,target_side,test_x+(target_side ? -4 : 4),sg::battle_commander_y,target_side ? 20 : -20,1);
        if(script>=2645 && script<=2647) projectile.target_offset_x=24*16;
        for(int tick=0;tick<12;++tick) game.battle_tick();
        assert(game.battle.sides[target_side].hp==100-sg::original::magic_general_hit_bonus(script));
        assert(!shot_alive(script));
        std::printf("PASS magic %d side%d hits general sharing a cell with dense soldiers\n",script,target_side);
    }
    // Ghost halberd must clear all enemy occupants, pass the general, then
    // clear his rear ranks; friendly and off-lane soldiers remain unharmed.
    for(int target_side : {0,1}) {
        siege();clear_soldiers();
        const int direction=target_side ? 1 : -1;
        game.battle.sides[1-target_side].x=target_side ? 1000 : 100;
        game.battle.sides[target_side].x=test_x;
        game.battle.sides[target_side].hp=100;
        game.battle.magic_callback_damage=8;
        place_soldier(target_side,test_x,sg::battle_commander_y);
        const auto first=game.battle.soldiers[target_side*side_slots];
        for(int i=0;i<20;++i) {
            auto& s=game.battle.soldiers[target_side*side_slots+i];
            s=first;s.index=uint8_t(i+1);s.action_ticks=200;
            // Ten share the general cell; ten occupy the rank behind him.
            s.x=int16_t(test_x+(i>=10 ? direction*80 : 0));
            s.previous_x=s.move_target_x=s.x;
        }
        auto& off_lane=game.battle.soldiers[target_side*side_slots+20];
        off_lane=first;off_lane.y=sg::battle_commander_y+90;
        off_lane.previous_y=off_lane.move_target_y=off_lane.y;off_lane.action_ticks=200;
        game.battle.sides[target_side].soldiers=game.battle.sides[target_side].front=21;
        place_soldier(1-target_side,test_x,sg::battle_commander_y);
        // Disable ordinary commander melee to isolate projectile filtering.
        for(auto& fighter:game.battle.sides) { fighter.action_ticks=200;fighter.attack_ticks=0; }
        game.battle.soldiers[(1-target_side)*side_slots].action_ticks=200;
        auto& shot=spawn(2635,target_side,test_x-direction*4,sg::battle_commander_y,direction*20,1);
        game.battle_tick();
        assert(shot.active && shot.collision_enabled==2 && shot.remaining==0);
        assert(sg::visual::camera_tracks(shot));
        assert(game.battle.sides[target_side].hp==92);
        for(int tick=0;tick<20;++tick) game.battle_tick();
        assert(game.battle.sides[target_side].hp==92);
        for(int i=0;i<20;++i) assert(game.battle.soldiers[target_side*side_slots+i].hp==0);
        assert(off_lane.hp>0);
        assert(game.battle.soldiers[(1-target_side)*side_slots].hp>0);
        assert(game.battle.sides[target_side].soldiers==1);
        assert(shot.active);
        assert(sg::visual::effect_visible(shot));
        for(int tick=0;tick<400;++tick) game.battle_tick();
        assert(!shot.active);
        std::printf("PASS ghost halberd side%d clears stacked rear ranks, one general hit, friendly/off-lane safe, expires\n",target_side);
    }
    std::puts("PASS projectile rules: miss keeps heading, crescent cuts soldiers, general hits terminate");
}
