#include "model.h"
#include <cassert>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
static sg::Game game;
int main(int argc,char** argv) {
    const int mode=argc==3 ? std::atoi(argv[2]) : 0;
    int route[sg::city_count],from=-1,goal=-1,n=0;
    for(int a=0;a<sg::city_count && from<0;++a) for(int b=0;b<sg::city_count;++b) {
        const int count=sg::map_route_cities(a,b,route,sg::city_count);
        if(count>=3 && !((sg::city_neighbors[a]>>b)&1)) {from=a;goal=b;n=count;break;}
    }
    assert(from>=0 && n>=3);
    const int hostile=route[1];
    game.new_game(1,1);
    for(auto& c:game.campaign.cities) {c.king=-1;c.major=-1;c.reserve_ticks=60000;}
    for(auto& o:game.campaign.officers) {o.status=sg::Status::absent;o.king=o.city=o.army=-1;}
    game.campaign.cities[from].king=game.campaign.cities[goal].king=1;
    game.campaign.cities[hostile].king=0;game.campaign.cities[hostile].major=1;
    for(int g=0;g<2;++g) {
        auto& o=game.campaign.officers[g];o.status=sg::Status::employed;
        o.king=int8_t(g ? 0 : 1);o.city=int16_t(g ? hostile : from);o.hp=uint16_t(game.max_hp(g));
    }
    game.campaign.phase=sg::Phase::world;
    const int general=0;
    assert(game.send_army(from,mode ? hostile : from,&general,1,false,false,0));
    if(mode) --game.campaign.armies[0].remaining;
    assert(game.redirect_army(0,goal));
    if(argc!=3) {
        const auto prepared=game;
        auto& gate=game.campaign.armies[0];gate.target=int8_t(hostile);gate.remaining=0;
        sg::army_pending_destination(game.campaign,0)=0;
        const auto checksum=game.checksum();
        assert(!game.redirect_army(0,goal) && game.checksum()==checksum);
        game=prepared;
    }
    if(argc==3) {
        FILE* f=std::fopen(argv[1],"wb");assert(f);
        assert(std::fwrite(&game,1,offsetof(sg::Game,battle_report),f)==offsetof(sg::Game,battle_report));
        std::fclose(f);std::printf("%d\n",hostile);return 0;
    }
    for(int tick=0;tick<10000 && game.campaign.phase==sg::Phase::world && game.campaign.armies[0].active;++tick) {
        game.campaign.month_ticks=0;game.world_tick();
    }
    if(game.campaign.phase!=sg::Phase::battle || game.battle.city!=hostile) {
        std::printf("FAIL route %d -> %d through hostile%d: phase%d city%d, officer reached%d\n",
            from,goal,hostile,int(game.campaign.phase),game.battle.city,game.campaign.officers[0].city);
        return 1;
    }
    assert(game.valid());
    std::puts("PASS: a multi-city march cannot pass through a defended hostile city");
}
