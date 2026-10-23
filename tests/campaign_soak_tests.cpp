#include "model.h"
#include "original_rules.h"
#include <cassert>
#include <cstdio>

namespace {
sg::Game game;
unsigned battles=0, deployments=0, battle_updates=0;
[[maybe_unused]] unsigned world_updates=0;
uint32_t trace=2166136261u;
void record_state(bool include_battle=false) {
    trace=(trace^game.checksum())*16777619u;
    if(include_battle) {
        const auto* bytes=reinterpret_cast<const uint8_t*>(&game.battle);
        for(unsigned i=0;i<sizeof(game.battle);++i) trace=(trace^bytes[i])*16777619u;
    }
}
void stable(const char* stage) {
    if(!game.valid()) {
        std::fprintf(stderr,"Invalid campaign at %s scenario=%d player=%d date=%d/%d phase=%d\n",
            stage,game.campaign.scenario,game.campaign.player,game.campaign.year,
            game.campaign.month,int(game.campaign.phase));
        for(int g=0;g<sg::general_count;++g) {
            const auto& o=game.campaign.officers[g];
            if(o.hp>game.max_hp(g) || o.mp>game.max_mp(g) || o.soldiers>game.max_soldiers(g) ||
               o.front>game.front_capacity(g) || o.soldiers-o.front>game.rear_capacity(g) ||
               (o.army>=0 && (!game.campaign.armies[o.army].active || o.city!=-1)))
                std::fprintf(stderr,"Officer %d hp=%u/%d mp=%u/%d troops=%u/%d front=%u/%d rearcap=%d city=%d army=%d status=%d\n",
                    g,o.hp,game.max_hp(g),o.mp,game.max_mp(g),o.soldiers,game.max_soldiers(g),o.front,
                    game.front_capacity(g),game.rear_capacity(g),o.city,o.army,int(o.status));
        }
        for(int city=0;city<sg::city_count;++city) {
            const auto& c=game.campaign.cities[city];
            if(c.reserve>sg::original::demake_reserve_capacity(city))
                std::fprintf(stderr,"City %d reserve=%u cap=%u population=%u\n",city,c.reserve,sg::original::demake_reserve_capacity(city),c.population);
        }
        for(int i=0;i<sg::max_armies;++i) {
            const auto& army=game.campaign.armies[i];
            if(!army.active) continue;
            std::fprintf(stderr,"Army %d king=%d count=%d route=%d/%d remaining=%u/%u retreat=%d goal=%d garrison=%d\n",
                i,army.king,army.count,army.origin,army.target,army.remaining,army.total,army.retreating,army.retreat_goal,army.garrisoned);
            for(int j=0;j<army.count;++j) {
                int g=army.generals[j];
                const auto& o=game.campaign.officers[g];
                std::fprintf(stderr,"  general=%d king=%d army=%d status=%d\n",g,o.king,o.army,int(o.status));
            }
        }
        std::fflush(stderr);
        assert(false);
    }
}
void manage_cities() {
    const int player=game.campaign.player;
    for(int city=0;city<sg::city_count;++city) if(game.campaign.cities[city].king==player) {
        int roster[sg::general_count];
        const int count=game.officers_at(city,player,roster,sg::general_count);
        for(int i=0;i<count;++i) {
            const int general=roster[i];
            game.transfer_soldiers(city,general,game.max_soldiers(general));
            for(int action=0;action<3;++action)
                if(game.domestic_action(city,general,action)) break;
        }
    }
}
[[maybe_unused]] void launch() {
    const int player=game.campaign.player;
    for(int city=0;city<sg::city_count;++city) if(game.campaign.cities[city].king==player) {
        int roster[sg::general_count], selected[5], count=0;
        const int available=game.officers_at(city,player,roster,sg::general_count);
        // Keep one officer home; launch rested, supplied officers by legal APIs.
        for(int i=0;i<available-1 && count<5;++i) {
            const int general=roster[i];
            const auto& officer=game.campaign.officers[general];
            if(officer.hp>=game.max_hp(general)*3/4 &&
               officer.soldiers>=sg::minimum(20,(game.max_soldiers(general)+1)/2))
                selected[count++]=general;
        }
        if(!count) continue;
        int neighbors[sg::city_count];
        const int adjacent=game.neighbors(city,neighbors);
        for(int i=0;i<adjacent;++i) if(game.campaign.cities[neighbors[i]].king!=player) {
            if(game.send_army(city,neighbors[i],selected,count)) ++deployments;
            break;
        }
    }
}
void fight(void (*tactics)(int,unsigned)=nullptr) {
    ++battles;
    unsigned updates=0;
    while(game.campaign.phase==sg::Phase::battle && updates<200000) {
        if(game.battle.row_setup_active) {
            const int controlled=game.campaign.officers[game.battle.sides[0].general].king==game.campaign.player ? 0 : 1;
            const int count=controlled ? game.battle.defender_count : game.battle.attacker_count;
            int best=-1,best_score=-1;
            for(int i=0;i<count;++i) if(game.battle.roster_active[controlled][i]) {
                const int g=controlled ? game.battle.defenders[i] : game.battle.attackers[i];
                const auto& o=game.campaign.officers[g];
                const int score=o.hp+o.soldiers*2+game.strength(g)*2+o.mp;
                if(score>best_score) { best=i; best_score=score; }
            }
            if(best>=0) {
                if(controlled) game.battle.defender_index=int8_t(best);
                else game.battle.attacker_index=int8_t(best);
            }
            // The real lineup-confirmation UI restores +10 HP/+5 MP for both
            // selected generals when it reloads the demake-style matchup.
            for(int side=0;side<2;++side) {
                const int index=side ? game.battle.defender_index : game.battle.attacker_index;
                const int g=side ? game.battle.defenders[index] : game.battle.attackers[index];
                auto& officer=game.campaign.officers[g];
                officer.hp=uint16_t(sg::minimum(game.max_hp(g),int(officer.hp)+10));
                officer.mp=uint16_t(sg::minimum(game.max_mp(g),int(officer.mp)+5));
            }
            game.start_duel();
        }
        // Match the UI's explicit next-match acknowledgement.
        game.battle.round_result_pending=0;
        const int side=game.campaign.officers[game.battle.sides[0].general].king==game.campaign.player ? 0 : 1;
        if(tactics) tactics(side,updates);
        else {
            if(!(updates%120)) game.issue_battle_command(side,sg::BattleCommand::assault);
            for(int slot=3;slot>=0;--slot) if(game.cast_skill(side,slot)) break;
        }
        game.battle_tick();
        ++updates;
    }
    battle_updates+=updates;
    assert(game.campaign.phase!=sg::Phase::battle && "Battle loop stalled");
    stable("battle settlement");
    record_state(true);
    if(game.campaign.phase==sg::Phase::prisoners) game.finish_prisoners();
    stable("prisoner settlement");
}
}
#ifndef SANGO_SOAK_NO_MAIN
int main() {
    unsigned campaigns=0,ended=0;
    for(int scenario=0;scenario<sg::scenario_count;++scenario)
        for(int player=0;player<sg::scenario_defs[scenario].king_count;++player) {
            if(!sg::king_defs[scenario][player].selectable) continue;
            game.new_game(scenario,player);
            stable("new game");
            const int end_year=game.campaign.year+20;
            unsigned ticks=0;
            while(game.campaign.phase!=sg::Phase::ended && game.campaign.year<end_year && ticks<75000) {
                if(game.campaign.phase==sg::Phase::domestic) {
                    manage_cities();
                    game.finish_domestic();
                    stable("domestic");
                }
                if(game.campaign.phase==sg::Phase::battle) { fight(); continue; }
                if(game.campaign.phase==sg::Phase::prisoners) game.finish_prisoners();
                if(game.campaign.phase==sg::Phase::world) {
                    if(!(ticks%300)) launch();
                    game.world_tick();
                    ++world_updates;
                    ++ticks;
                    if(!(ticks%300) || game.campaign.phase!=sg::Phase::world) {
                        stable("world"); record_state();
                    }
                }
            }
            stable("campaign end");
            assert(game.campaign.year>=end_year || game.campaign.phase==sg::Phase::ended);
            if(game.campaign.phase==sg::Phase::ended) ++ended;
            ++campaigns;
            std::printf("Campaign %u scenario=%d player=%d reached %d/%d phase=%d world=%u battles=%u\n",
                campaigns,scenario,player,game.campaign.year,game.campaign.month,int(game.campaign.phase),ticks,battles);
            std::fflush(stdout);
            std::printf("State trace %08x\n",trace);
        }
    assert(campaigns==28 && deployments && battles && world_updates>100000);
    std::printf("PASS: %u legal campaigns for 20 years or natural ending; %u ended, %u deployments, %u battles, %u world and %u battle updates\n",
        campaigns,ended,deployments,battles,world_updates,battle_updates);
}
#endif
