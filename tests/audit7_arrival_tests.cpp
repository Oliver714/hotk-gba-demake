#include "model.h"
#include <cassert>
#include <cstdio>
static sg::Game game;
int main() {
    for(int capacity=0;capacity<=5;++capacity) for(int incoming=1;incoming<=5;++incoming) {
        game.new_game(1,1);
        for(auto& c:game.campaign.cities) { c.major=-1;c.reserve_ticks=100; }
        for(auto& o:game.campaign.officers) {o.status=sg::Status::absent;o.king=o.city=o.army=-1;}
        const int from=5,to=sg::city_neighbor_order[from][0];
        game.campaign.cities[from].king=game.campaign.cities[to].king=1;
        for(int i=0;i<10-capacity;++i) {
            auto& o=game.campaign.officers[10+i];o.status=sg::Status::employed;o.king=1;o.city=int16_t(to);
        }
        int generals[5]={0,1,2,3,4};
        for(int i=0;i<incoming;++i) {
            auto& o=game.campaign.officers[i];o.status=sg::Status::employed;o.king=1;o.city=from;o.hp=uint16_t(game.max_hp(i));
        }
        game.campaign.phase=sg::Phase::world;
        assert(game.send_army(from,to,generals,incoming,false,false,123));
        auto& a=game.campaign.armies[0];
        auto& captive=game.campaign.officers[30];
        captive.status=sg::Status::prisoner;captive.king=1;captive.city=-1;captive.army=0;
        const auto money=game.campaign.cities[to].money+a.money;
        a.remaining=0;game.world_tick();
        const int entered=sg::minimum(capacity,incoming);
        for(int i=0;i<incoming;++i) {
            const auto& o=game.campaign.officers[i];
            if((i<entered && (o.city!=to || o.army!=-1)) ||
               (i>=entered && (o.city!=-1 || o.army!=0))) {
                std::printf("FAIL capacity%d incoming%d general%d city%d army%d\n",capacity,incoming,i,o.city,o.army);
                return 1;
            }
        }
        assert(a.count==incoming-entered);
        for(int i=0;i<a.count;++i) assert(a.generals[i]==entered+i);
        assert(a.active==(entered<incoming));
        assert(game.campaign.cities[to].money+(a.active ? a.money : 0)==money);
        assert(captive.army==(a.active ? 0 : -1));
        assert(captive.city==(a.active ? -1 : to));
        assert(game.valid());
    }
    std::puts("PASS: 30 partial/full arrivals, roster order, money and prisoners conserved");
}
