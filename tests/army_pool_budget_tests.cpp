#include "model.h"
#include <cassert>
#include <cstdio>
static sg::Game game;
int main() {
    game.new_game(1,1);
    game.campaign.phase=sg::Phase::world;
    for(auto& o:game.campaign.officers) {
        o.status=sg::Status::absent; o.king=-1; o.city=-1; o.army=-1;
    }
    for(auto& a:game.campaign.armies) a={};
    game.campaign.cities[5].king=1;
    int adjacent[sg::city_count]; assert(game.neighbors(5,adjacent)>0);
    const int ai_city=adjacent[0]; game.campaign.cities[ai_city].king=0;
    for(int g=0;g<3;++g) {
        auto& o=game.campaign.officers[g];
        o.status=sg::Status::employed; o.king=1; o.city=5;
    }
    for(int g=3;g<158;++g) {
        auto& o=game.campaign.officers[g];
        o.status=sg::Status::employed; o.king=0; o.city=int16_t(ai_city);
    }
    for(int i=0;i<30;++i) {
        const int selected[5]={3+i*5,4+i*5,5+i*5,6+i*5,7+i*5};
        assert(game.send_army(ai_city,ai_city,selected,5,false,true,0));
    }
    assert(game.valid());
    const int extra[5]={153,154,155,156,157};
    uint32_t before=game.checksum();
    assert(!game.send_army(ai_city,ai_city,extra,5,false,true,0));
    assert(game.checksum()==before);
    for(int g=0;g<2;++g) assert(game.send_army(5,5,&g,1,false,false,0));
    assert(game.valid());
    const int third=2;
    before=game.checksum();
    assert(!game.send_army(5,5,&third,1,false,false,0));
    assert(game.checksum()==before); // Packed five-person AI rosters stay intact.
    assert(game.enter_army_city(30));
    before=game.checksum();
    assert(!game.send_army(ai_city,ai_city,extra,5,false,true,0));
    assert(game.checksum()==before);
    assert(game.send_army(5,5,&third,1,false,false,0));
    assert(game.valid());
    std::puts("PASS: 30 packed AI garrisons leave two player slots; refused allocation preserves all state; returned slot remains available to player");

    // Legacy saturated pool: all 32 groups are full, so merging cannot help.
    // A neighboring friendly city has room; overflow must physically travel,
    // preserving its cargo, before ordinary arrival can release a slot.
    for(int g=0;g<3;++g) {
        auto& o=game.campaign.officers[g]; o.city=5; o.army=-1;
    }
    for(int i=30;i<32;++i) {
        auto& a=game.campaign.armies[i]; a={};
        a.active=a.garrisoned=1; a.king=0; a.origin=a.target=int8_t(ai_city);
        a.retreat_goal=-1; a.count=5; a.money=1000;
        for(int n=0;n<5;++n) {
            const int g=3+i*5+n; a.generals[n]=int16_t(g);
            auto& o=game.campaign.officers[g];
            o.status=sg::Status::employed; o.king=0; o.city=-1; o.army=int16_t(i);
        }
    }
    for(int g=163;g<173;++g) {
        auto& o=game.campaign.officers[g];
        o.status=sg::Status::employed; o.king=0; o.city=int16_t(ai_city); o.army=-1;
    }
    auto& captive=game.campaign.officers[173];
    captive.status=sg::Status::prisoner; captive.king=0; captive.city=-1; captive.army=31;
    int neighbors[sg::city_count]; const int adjacent_count=game.neighbors(ai_city,neighbors);
    int vacancy=-1;
    for(int i=0;i<adjacent_count;++i) if(neighbors[i]!=5) { vacancy=neighbors[i]; break; }
    assert(vacancy>=0); game.campaign.cities[vacancy].king=0;
    game.campaign.month=1; game.campaign.month_ticks=299;
    assert(game.valid());
    game.world_tick();
    assert(!game.campaign.armies[31].garrisoned && game.campaign.armies[31].remaining>0);
    assert(game.campaign.armies[31].money==1000 && captive.army==31 && captive.city==-1);
    bool vacancy_seen=false;
    for(int tick=0;tick<3000 && !vacancy_seen;++tick) {
        game.world_tick(); assert(game.valid());
        for(const auto& a:game.campaign.armies) vacancy_seen|=!a.active;
    }
    assert(vacancy_seen);
    const int commander=0;
    assert(game.send_army(5,5,&commander,1,false,false,0));
    assert(game.valid());
    std::puts("PASS: legacy 32 full garrisons march to real vacancies, preserving cargo; ordinary arrival restores player deployment");
}
