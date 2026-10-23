#include "city_navigation.h"
#include <cassert>
#include <cstdio>
int main() {
    constexpr uint64_t all=(uint64_t(1)<<sg::city_count)-1;
    int checks=0;
    for(int from=0;from<sg::city_count;++from) {
        for(int axis=0;axis<4;++axis) for(int subset=-1;subset<sg::city_count;++subset) {
            const int dx=axis<2 ? (axis ? 1 : -1) : 0;
            const int dy=axis>=2 ? (axis==3 ? 1 : -1) : 0;
            const uint64_t allowed=subset<0 ? all : uint64_t(1)<<subset;
            int expected=from,distance=10000000;
            for(int i=0;i<sg::city_count;++i) if((allowed>>i)&1) {
                const int x=int(sg::city_defs[i].x)-sg::city_defs[from].x;
                const int y=int(sg::city_defs[i].y)-sg::city_defs[from].y;
                if(x*dx+y*dy<=0) continue;
                const int d=x*x+y*y+(dx ? y*y*3 : x*x*3);
                if(d<distance) { expected=i;distance=d; }
            }
            assert(sg::directional_city(from,dx,dy,allowed)==expected);++checks;
        }
        uint64_t adjacent=0;
        for(int n=0;n<sg::city_neighbor_counts[from];++n) {
            const int target=sg::city_neighbor_order[from][n];
            if(sg::city_route_lengths[from][target]!=0xffff) adjacent|=uint64_t(1)<<target;
        }
        assert(sg::adjacent_reachable_cities(from,all)==adjacent);
        assert(sg::adjacent_reachable_cities(from,0)==0);
        for(int target=0;target<sg::city_count;++target)
            assert(sg::adjacent_reachable_cities(from,uint64_t(1)<<target)==(adjacent&(uint64_t(1)<<target)));
    }
    std::printf("PASS %d geographic navigation cases, all adjacency masks\n",checks);
}
