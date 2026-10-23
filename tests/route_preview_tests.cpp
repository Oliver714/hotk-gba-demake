#include "model.h"
#include <cassert>
#include <cstdio>
int main() {
    int checked=0,indirect=0;
    for(int from=0;from<sg::city_count;++from) for(int to=0;to<sg::city_count;++to) {
        int path[sg::city_count],queue[sg::city_count],previous[sg::city_count];
        for(int& p:previous) p=-1;
        int head=0,tail=0;queue[tail++]=from;previous[from]=from;
        while(head<tail) {
            const int c=queue[head++];
            for(int next=0;next<sg::city_count;++next)
                if(previous[next]<0 && ((sg::city_neighbors[c]>>next)&1)) {
                    previous[next]=c;queue[tail++]=next;
                }
        }
        const int count=sg::map_route_cities(from,to,path,sg::city_count);
        assert(count>0 && path[0]==from && path[count-1]==to);
        int c=to;
        for(int i=count-1;i>=0;--i) { assert(path[i]==c);c=previous[c]; }
        for(int i=1;i<count;++i) {
            assert((sg::city_neighbors[path[i-1]]>>path[i])&1);
            assert(sg::city_routes[path[i-1]][path[i]].count>1);
        }
        if(count>2) { assert(!sg::city_routes[from][to].count);++indirect; }
        if(count>1) assert(!sg::map_route_cities(from,to,path,count-1));
        ++checked;
    }
    int path[48];
    assert(!sg::map_route_cities(-1,0,path,48));assert(!sg::map_route_cities(0,48,path,48));
    assert(!sg::map_route_cities(0,1,nullptr,48));
    std::printf("PASS %d route previews match independent BFS and real road segments, including %d indirect pairs\n",checked,indirect);
}
