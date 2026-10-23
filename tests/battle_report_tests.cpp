#include "model.h"
#include "original_rules.h"
#include <cassert>
#include <cstdio>
#include <cstddef>

namespace {
sg::Game game;
int own[2], opponent;
int home, away;
void fixture(bool player_defends=false,bool city_battle=false) {
    game.new_game(1,1);
    for(auto& army:game.campaign.armies) army={};
    for(auto& officer:game.campaign.officers) {
        officer.status=sg::Status::absent; officer.king=-1;
        officer.city=officer.army=-1;
    }
    for(auto& city:game.campaign.cities) { city.king=-1; city.major=-1; }
    home=5;
    int adjacent[sg::city_count];
    assert(game.neighbors(home,adjacent)>0);
    away=adjacent[0];
    game.campaign.cities[home].king=1;
    game.campaign.cities[away].king=0;
    int chosen=0;
    for(int g=0;g<sg::general_count && chosen<3;++g) {
        if(g==sg::king_defs[1][0].general || g==sg::king_defs[1][1].general) continue;
        if(chosen<2) own[chosen]=g; else opponent=g;
        ++chosen;
    }
    for(int i=0;i<3;++i) {
        const int g=i<2 ? own[i] : opponent;
        auto& officer=game.campaign.officers[g];
        officer.status=sg::Status::employed;
        officer.king=i<2 ? 1 : 0;
        officer.city=int16_t(i<2 ? home : away);
        officer.rank=25;
        officer.exp=uint32_t(sg::original::experience_threshold(25));
        officer.hp=uint16_t(game.max_hp(g));
        officer.soldiers=10; officer.front=5;
    }
    game.campaign.cities[home].major=int16_t(own[0]);
    game.campaign.cities[away].major=int16_t(opponent);
    game.campaign.phase=sg::Phase::world;
    if(city_battle) {
        assert(game.send_army(away,home,&opponent,1));
        game.start_battle(0);
    } else if(player_defends) {
        assert(game.send_army(away,home,&opponent,1));
        assert(game.send_army(home,away,own,2));
        game.start_field_battle(0,1);
    } else {
        assert(game.send_army(home,away,own,2));
        assert(game.send_army(away,home,&opponent,1));
        game.start_field_battle(0,1);
    }
    assert(game.campaign.phase==sg::Phase::battle);
    assert(game.valid());
}
void capture_first() {
    auto& officer=game.campaign.officers[own[0]];
    officer.status=sg::Status::prisoner; officer.king=0;
    officer.city=officer.army=-1;
    game.battle.captured[0]=int16_t(own[0]);
    game.battle.captured_count=1;
}
void check_loss(int winner) {
    game.finish_battle(winner);
    assert(game.valid());
    const auto& report=game.battle_report;
    assert(report.pending && report.result==1 && report.count==2);
    assert(report.captured_count==1);
    assert(report.entries[0].general==own[0]);
    assert(report.entries[0].outcome==sg::BattleOfficerOutcome::captured);
    assert(report.entries[0].king==0);
    assert(game.campaign.officers[own[0]].status==sg::Status::prisoner);
    assert(report.entries[1].general==own[1]);
    assert(report.entries[1].outcome==sg::BattleOfficerOutcome::retreating);
    assert(game.campaign.officers[own[1]].king==1);
}
}
int main(int argc,char** argv) {
    fixture(); capture_first(); check_loss(1);
    fixture(true); capture_first(); check_loss(0);
    fixture(true,true); capture_first(); check_loss(0);
    fixture();
    game.finish_battle(1);
    assert(game.battle_report.pending && game.battle_report.result==1);
    assert(game.battle_report.captured_count==0);
    for(int i=0;i<2;++i) assert(game.battle_report.entries[i].outcome==sg::BattleOfficerOutcome::retreating);
    fixture(); capture_first();
    game.finish_battle(0);
    assert(game.battle_report.result==0 && game.battle_report.captured_count==0);
    assert(game.battle_report.entries[0].outcome==sg::BattleOfficerOutcome::rescued);
    fixture();
    game.finish_battle(2);
    assert(game.battle_report.pending && game.battle_report.result==2 && game.battle_report.captured_count==0);
    fixture(); capture_first();
    auto& last=game.campaign.officers[own[1]];
    last.status=sg::Status::prisoner; last.king=0; last.city=last.army=-1;
    game.battle.captured[1]=int16_t(own[1]); game.battle.captured_count=2;
    game.campaign.cities[home].king=0;
    game.finish_battle(1);
    assert(game.campaign.phase==sg::Phase::ended);
    assert(game.battle_report.pending && game.battle_report.captured_count==2);
    fixture();
    game.campaign.player=2;
    game.finish_battle(1);
    assert(!game.battle_report.pending);
    game.battle_report.pending=1;
    game.new_game(1,1);
    assert(!game.battle_report.pending && game.battle_report.count==0);
    if(argc>1) {
        // Feed the actual ARM battle update a two-officer field battle. The
        // reserve has already withdrawn; the current officer falls with enough
        // rear troops for native capture to be certain, leaving a mixed report.
        fixture();
        game.battle.roster_active[0][1]=0;
        game.battle.roster_active_count[0]=1;
        game.battle.attacker_index=0;
        game.battle.row_setup_active=0;
        game.start_duel();
        game.battle.sides[0].hp=0;
        game.battle.sides[0].front=0;
        game.battle.sides[1].mp=0;
        auto* file=std::fopen(argv[1],"wb"); assert(file);
        const auto bytes=offsetof(sg::Game,battle_report);
        assert(std::fwrite(&game,1,bytes,file)==bytes);
        std::fclose(file);
    }
    std::puts("PASS: battle reports preserve both original player rosters through field/city defeat, distinguish capture/retreat/rescue/draw, and reset for a new game");
}
