#include "model.h"
#include "original_rules.h"
#include <cassert>
#include <cstdio>

namespace {
sg::Game game;

void siege(int attacker_count=2,int defender_count=2) {
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
        const int count=side ? defender_count : attacker_count;
        for(int i=0;i<count;++i) {
            auto& officer=game.campaign.officers[side*2+i];
            officer.status=sg::Status::employed;
            officer.king=side ? 0 : 1;
            officer.city=side ? target : 5;
            officer.hp=60; officer.mp=0; officer.front=officer.soldiers=0;
        }
    }
    game.campaign.phase=sg::Phase::world;
    int attackers[2]={0,1};
    assert(game.send_army(5,target,attackers,attacker_count));
    game.start_battle(0);
    assert(game.campaign.phase==sg::Phase::battle);
    game.start_duel(); // Confirm the initial lineup.
}
}

#ifndef SANGO_ROUND_NO_MAIN
int main() {
    {
        siege(1,2);
        auto& victor=game.campaign.officers[0];
        victor.rank=2; victor.exp=300;
        game.campaign.officers[2].exp=game.campaign.officers[3].exp=200;
        game.campaign.officers[2].rank=game.campaign.officers[3].rank=2;
        game.resolve_duel(0);
        // demake thresholds: lv2=200, lv3=377, lv4=771. First win adds
        // 177/2=88 and levels immediately, before the next selection.
        assert(victor.exp==388 && victor.rank==3);
        assert(game.battle.round_result_level_before==2);
        assert(game.campaign.officers[2].exp==200);
        assert(game.battle.row_setup_active && game.battle.roster_active_count[1]==1);
        assert(game.battle.experience_rewards[0][0]==0);
        game.start_duel();
        game.resolve_duel(0);
        // Second win adds 394/2=197, then low-level army victory adds 50.
        assert(victor.exp==635 && victor.rank==3);
        assert(game.campaign.officers[3].exp==200);
    }
    {
        siege(2,1);
        auto& victor=game.campaign.officers[0];
        victor.rank=6; victor.exp=1970;
        game.campaign.officers[1].rank=2; game.campaign.officers[1].exp=200;
        game.resolve_duel(0);
        // lv6=1970, lv7=2775: duel and army victory each grant 402.
        assert(victor.exp==2774 && victor.rank==6);
        assert(game.campaign.officers[1].exp==250); // Unselected winner gets only army bonus.
    }
    {
        siege(1,1);
        game.campaign.officers[0].rank=2; game.campaign.officers[0].exp=300;
        game.resolve_duel(2);
        assert(game.campaign.officers[0].exp==300 && game.campaign.officers[0].rank==2);
        siege(1,1);
        game.campaign.officers[0].exp=300;
        game.battle.sides[0].order=sg::Order::retreat;
        game.resolve_duel(1);
        assert(game.campaign.officers[0].exp==300); // Retreat does not farm native loser EXP.
    }
    // demake's front/back position moves the selected player commander toward
    // the enemy before each matchup; the GBA setup choice must change its
    // actual deployment coordinate on either army side.
    for(int side=0;side<2;++side) {
        siege();
        const int index=side ? game.battle.defender_index : game.battle.attacker_index;
        const int original_x=game.battle.sides[side].x;
        const int enemy_x=game.battle.sides[1-side].x;
        game.battle.general_front[side][index]=1;
        game.start_duel();
        const int direction=side ? 1 : -1;
        assert(game.battle.sides[side].x==original_x+direction*sg::battle_general_front_offset);
        assert(game.battle.sides[1-side].x==enemy_x);
    }

    // Background AI marches and sieges must not claim the player has sent an
    // army or finished a battle. A real player battle must still notify.
    siege();
    game.campaign.phase=sg::Phase::world;
    const int source=game.campaign.armies[0].origin;
    const int ai_target=game.campaign.armies[0].target;
    game.campaign.armies[0].king=0;
    game.campaign.cities[source].king=0;
    game.campaign.cities[ai_target].king=2;
    for(int g=0;g<4;++g) game.campaign.officers[g].king=int8_t(g<2 ? 0 : 2);
    for(int city=0;city<sg::city_count;++city) if(city!=source && city!=ai_target) {
        game.campaign.cities[city].king=1;
        break;
    }
    auto& ai_marcher=game.campaign.officers[10];
    ai_marcher.status=sg::Status::employed; ai_marcher.king=0;
    ai_marcher.city=int16_t(source); ai_marcher.army=-1; ai_marcher.hp=60;
    const char* unchanged="existing player message";
    game.notice=unchanged;
    int marcher=10;
    assert(game.send_army(source,ai_target,&marcher,1,true));
    assert(game.notice==unchanged);
    game.start_battle(0);
    assert(game.campaign.phase==sg::Phase::world && game.notice==unchanged);
    siege(1,1);
    game.notice=unchanged;
    game.battle.sides[1].hp=0;
    game.resolve_duel(0);
    assert(game.notice!=unchanged);
    // Charge cannot create MP within a bout. A confirmed new bout grants only
    // demake's capped +5 MP; isolate the spell-budget probes after checking it.
    for(int side=0;side<2;++side) {
        siege();
        game.campaign.player=uint8_t(side ? 0 : 1);
        const auto& spell=sg::skill_defs[0];
        const int g=side*2;
        assert(spell.mp>0 && spell.power==100);
        game.campaign.officers[g].learned[0]=spell.id;
        game.campaign.officers[g].mp=uint16_t(2*spell.mp);
        game.start_duel();
        auto& own=game.battle.sides[side];
        assert(own.mp==sg::minimum(game.max_mp(g),2*spell.mp+5));
        own.mp=game.campaign.officers[g].mp=uint16_t(2*spell.mp);
        own.charge=sg::original::cast_charge_max;
        game.battle.paused=1;
        assert(!game.cast_skill(side,0) && own.mp==2*spell.mp);
        game.battle.paused=0;
        own.charge=sg::original::cast_charge_max-1;
        assert(!game.cast_skill(side,0) && own.mp==2*spell.mp);
        own.charge=sg::original::cast_charge_max;
        assert(game.cast_skill(side,0));
        assert(own.mp==spell.mp && game.campaign.officers[g].mp==spell.mp && !own.charge);
        game.battle.sides[1-side].hp=0;
        game.resolve_duel(side);
        assert(game.battle.row_setup_active);
        assert(!game.cast_skill(side,0));
        if(side) game.battle.defender_index=0;
        else game.battle.attacker_index=0;
        game.start_duel();
        assert(own.general==g && own.mp==sg::minimum(game.max_mp(g),spell.mp+5));
        own.mp=game.campaign.officers[g].mp=uint16_t(spell.mp);
        auto& enemy=game.battle.sides[1-side];
        enemy.order=sg::Order::hold; enemy.soldiers=1; enemy.mp=0;
        enemy.ai_action_flags|=sg::fighter_opening_checked_flag|sg::fighter_opening_finished_flag;
        game.battle.ai_decision_countdown[1-side]=60000;
        const int charge_gain=sg::maximum(game.strength(g),game.intelligence(g));
        const int recharge_ticks=(sg::original::cast_charge_max+charge_gain-1)/charge_gain;
        for(int tick=0;tick<recharge_ticks+120;++tick) game.battle_tick();
        assert(own.mp==spell.mp && own.charge==sg::original::cast_charge_max);
        assert(game.cast_skill(side,0));
        assert(own.mp==0 && game.campaign.officers[g].mp==0);
        for(int tick=0;tick<recharge_ticks+120;++tick) game.battle_tick();
        assert(own.mp==0 && own.charge==sg::original::cast_charge_max);
        assert(!game.cast_skill(side,0) && enemy.mp==0);
        game.start_duel();
        assert(own.mp==sg::minimum(game.max_mp(g),5) && game.campaign.officers[g].mp==own.mp);
    }
    // A surviving general who lost every soldier can be selected again, but
    // must wait for the player's new order, in either attacker/defender role.
    for(int side=0;side<2;++side) {
        siege();
        game.campaign.player=uint8_t(side ? 0 : 1);
        game.start_duel();
        const int retained_general=game.battle.sides[side].general;
        assert(game.issue_battle_order(side,sg::Order::advance));
        game.battle.sides[1-side].hp=0;
        game.resolve_duel(side);
        assert(game.battle.row_setup_active && game.battle.roster_active[side][0]);
        if(side) game.battle.defender_index=0;
        else game.battle.attacker_index=0;
        game.start_duel();
        auto& own=game.battle.sides[side];
        auto& enemy=game.battle.sides[1-side];
        assert(own.general==retained_general && own.order==sg::Order::hold && !own.soldiers);
        enemy.order=sg::Order::hold;
        enemy.soldiers=1; // Keep the distant opponent stationary for this probe.
        const int initial_x=own.x;
        const sg::Order waiting_orders[]={sg::Order::hold,sg::Order::front,sg::Order::rear};
        for(auto order : waiting_orders) {
            assert(game.issue_battle_order(side,order));
            for(int tick=0;tick<60;++tick) game.battle_tick();
            assert(own.x==initial_x && !game.battle.general_visual[side].motion_hold);
        }
        assert(game.issue_battle_order(side,sg::Order::general));
        for(int tick=0;tick<120;++tick) game.battle_tick();
        assert((int(own.x)-initial_x)*(side ? 1 : -1)>0);
    }
    // Previous generals' elapsed time must not expire a newly deployed pair.
    siege();
    game.battle.tick=sg::original::battle_timeout_updates+300;
    game.battle.duel_tick=0;
    game.battle_tick();
    assert(game.battle.pending_winner<0 && game.battle.duel_tick==1);

    // demake's time-up returns to lineup selection without eliminating either
    // general. The player's selected fighter remains selected and the AI
    // chooses a fresh opponent.
    game.battle.duel_tick=sg::original::battle_timeout_updates-1;
    const int hp0=game.battle.sides[0].hp, hp1=game.battle.sides[1].hp;
    game.battle_tick();
    assert(game.battle.pending_winner==2);
    for(int i=0;i<46;++i) game.battle_tick();
    assert(game.campaign.phase==sg::Phase::battle && game.battle.row_setup_active);
    assert(game.battle.roster_active_count[0]==2 && game.battle.roster_active_count[1]==2);
    assert(game.battle.roster_active[0][0] && game.battle.roster_active[1][0]);
    assert(game.battle.attacker_index==0 && game.battle.defender_index<2);
    assert(game.campaign.officers[0].hp==hp0 && game.campaign.officers[2].hp==hp1);
    assert(game.battle.experience_rewards[0][0]==0 && game.battle.experience_rewards[1][0]==0);
    assert(!game.battle.round_result_pending);
    const int elapsed=game.battle.tick;
    for(int i=0;i<120;++i) game.battle_tick();
    assert(game.battle.tick==elapsed && game.battle.roster_active_count[0]==2);
    game.start_duel();
    assert(!game.battle.row_setup_active && game.battle.duel_tick==0);
    game.battle_tick();
    assert(game.battle.pending_winner<0 && game.battle.duel_tick==1);

    // A draw with one fighter on each side still returns to lineup selection.
    siege(1,1);
    game.resolve_duel(2);
    assert(game.campaign.phase==sg::Phase::battle && game.battle.row_setup_active);
    assert(game.battle.roster_active_count[0]==1 && game.battle.roster_active_count[1]==1);

    // A draw also preserves the remainder of an unequal roster.
    siege(2,1);
    const int target=game.battle.city;
    game.resolve_duel(2);
    assert(game.campaign.phase==sg::Phase::battle && game.campaign.cities[target].king==0);
    assert(game.battle.roster_active_count[0]==2 && game.battle.roster_active_count[1]==1);
    siege(1,2);
    const int second_target=game.battle.city;
    game.resolve_duel(2);
    assert(game.campaign.phase==sg::Phase::battle && game.campaign.cities[second_target].king==0);
    assert(game.battle.roster_active_count[0]==1 && game.battle.roster_active_count[1]==2);

    // A general defeated on the limit update remains a defeat, not a draw.
    siege();
    game.battle.duel_tick=sg::original::battle_timeout_updates-1;
    game.battle.sides[1].hp=0;
    game.battle_tick();
    assert(game.battle.pending_winner==0);
    for(int i=0;i<46;++i) game.battle_tick();
    assert(game.battle.row_setup_active && game.battle.roster_active_count[0]==2 &&
           game.battle.roster_active_count[1]==1);
    const auto rewards=game.battle.experience_rewards[0][0];
    for(int i=0;i<120;++i) game.battle_tick();
    assert(game.battle.roster_active_count[1]==1 && game.battle.experience_rewards[0][0]==rewards);
    std::puts("Battle round regressions passed.");
}
#endif
