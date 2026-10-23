#ifndef SANGO_CITY_NAVIGATION_H
#define SANGO_CITY_NAVIGATION_H
#include "data.h"
namespace sg {
// Shared geographic navigation. Perpendicular distance is weighted x4;
// strict comparison preserves city-ID tie breaking on every map page.
inline int directional_city(int current,int dx,int dy,uint64_t allowed) {
    int nearest=current,best=10000000;
    for(int i=0;i<city_count;++i) {
        if(!((allowed>>i)&1)) continue;
        const int x=int(city_defs[i].x)-city_defs[current].x;
        const int y=int(city_defs[i].y)-city_defs[current].y;
        if(x*dx+y*dy<=0) continue;
        const int score=dx ? x*x+4*y*y : 4*x*x+y*y;
        if(score<best) { nearest=i;best=score; }
    }
    return nearest;
}
inline uint64_t adjacent_reachable_cities(int origin,uint64_t reachable) {
    uint64_t result=0;
    for(int n=0;n<city_neighbor_counts[origin];++n) {
        const int target=city_neighbor_order[origin][n];
        if(city_route_lengths[origin][target]!=0xffff) result|=uint64_t(1)<<target;
    }
    return result&reachable;
}
}
#endif
