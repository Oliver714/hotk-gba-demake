#include "model.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static sg::Game game;
static int case_from,case_to,case_goal;
static sg::MapPoint position(const sg::Army& a) {
    return sg::army_position(game.campaign,int(&a-game.campaign.armies));
}
static void same(sg::MapPoint a,sg::MapPoint b) {
    if(std::abs(a.x-b.x)>1 || std::abs(a.y-b.y)>1)
        std::fprintf(stderr,"position discontinuity from=%d to=%d goal=%d: %d,%d -> %d,%d route=%d,%d ticks=%d/%d\n",
            case_from,case_to,case_goal,a.x,a.y,b.x,b.y,game.campaign.armies[0].origin,game.campaign.armies[0].target,
            game.campaign.armies[0].remaining,game.campaign.armies[0].total);
    assert(std::abs(a.x-b.x)<=1 && std::abs(a.y-b.y)<=1);
}
static void setup(int from,int to) {
    game.new_game(4,0);
    game.campaign.phase=sg::Phase::world;
    for(auto& a:game.campaign.armies) a={};
    for(auto& o:game.campaign.officers) { o.status=sg::Status::absent;o.city=o.army=o.king=-1; }
    for(auto& c:game.campaign.cities) { c.king=0;c.major=-1; }
    game.campaign.cities[47].king=1;
    for(int k=0;k<2;++k) {
        auto& ruler=game.campaign.officers[sg::king_defs[4][k].general];
        ruler.king=int8_t(k);ruler.city=int16_t(k ? 47 : from);ruler.status=sg::Status::employed;
    }
    auto& o=game.campaign.officers[0];
    o.status=sg::Status::employed;o.city=int16_t(from);o.king=0;o.hp=uint16_t(game.max_hp(0));
    game.campaign.cities[from].king=0;game.campaign.cities[from].major=0;
    int general=0;assert(game.send_army(from,to,&general,1));
    game.campaign.phase=sg::Phase::world;
}
int main() {
    int checked=0;
    for(int from=0;from<sg::city_count;from+=7) {
        const int to=sg::city_neighbor_order[from][0];
        for(int fraction=1;fraction<=3;++fraction) for(int goal=0;goal<sg::city_count;++goal) {
            case_from=from;case_to=to;case_goal=goal;
            setup(from,to);
            auto& a=game.campaign.armies[0];a.remaining=uint16_t(a.total*(4-fraction)/4);
            const auto before=position(a);const auto remaining=a.remaining,total=a.total;
            assert(game.garrison_army(0));same(before,position(a));
            assert(a.remaining==remaining && a.total==total && game.valid());
            for(int tick=0;tick<20;++tick) game.world_tick();
            same(before,position(a));assert(a.remaining==remaining);
            sg::Campaign saved=game.campaign;game.campaign={};game.campaign=saved;
            assert(game.valid());same(before,position(a));
            assert(game.resume_army(0));assert(a.remaining==remaining && !a.garrisoned);
            assert(game.garrison_army(0));
            const auto untouched=game.campaign;
            sg::Army preview;uint8_t pending=0;
            assert(game.preview_army_redirect(0,goal,preview,pending));
            assert(std::memcmp(&untouched,&game.campaign,sizeof(untouched))==0);
            const auto live_preview=sg::army_route_position(preview,preview.total-preview.remaining,(pending&0x80)!=0);
            same(before,live_preview);
            assert(game.redirect_army(0,goal));
            assert(std::memcmp(&preview,&a,sizeof(a))==0);
            assert(sg::army_pending_destination(game.campaign,0)==pending);
            assert(sg::army_destination(game.campaign,0)==goal);
            uint32_t expected_remaining=a.remaining;
            if(goal!=a.target) {
                int route[sg::city_count];const int n=sg::map_route_cities(a.target,goal,route,sg::city_count);
                assert(n>=2);int distance=0;
                for(int i=1;i<n;++i) distance+=sg::city_route_lengths[route[i-1]][route[i]];
                expected_remaining+=sg::minimum(65535,120+distance);
            }
            const auto query_before=game.campaign;
            assert(game.army_remaining_travel(0)==expected_remaining);
            assert(std::memcmp(&query_before,&game.campaign,sizeof(query_before))==0);
            same(before,position(a));assert(!a.garrisoned && game.valid());
            for(int tick=0;tick<20000 && a.active && game.campaign.phase==sg::Phase::world;++tick) {
                game.campaign.month_ticks=0;game.world_tick();
                assert(game.valid());
            }
            if(!a.active) assert(game.campaign.officers[0].city==goal);
            else assert(game.campaign.phase==sg::Phase::battle && a.target==goal);
            ++checked;
        }
    }
    setup(5,sg::city_neighbor_order[5][0]);
    auto& a=game.campaign.armies[0];a.remaining=a.total/2;
    assert(game.army_near_city(0)<0);
    const auto before=a;
    assert(!game.enter_army_city(0));assert(std::memcmp(&before,&a,sizeof(a))==0);
    assert(game.garrison_army(0));
    // A paused army on a road is not invulnerable to a moving hostile army.
    auto& b=game.campaign.armies[1];b=a;b.garrisoned=0;b.king=1;b.generals[0]=1;
    auto& enemy=game.campaign.officers[1];enemy=game.campaign.officers[0];enemy.king=1;enemy.army=1;
    game.world_tick();assert(game.campaign.phase==sg::Phase::battle && game.battle.field_battle);
    std::printf("PASS %d live-position stop/resume/redirect/save/arrival cases, near-city entry and stationary encounter\n",checked);
}
