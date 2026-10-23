#include "map_markers.h"
#include "data.h"
#include <cassert>
#include <cstdio>
int main() {
    int checked=0;
    constexpr int heights[]={96,112};
    for(int height:heights) {
        sg::MapMarkerPoint cities[sg::city_count];
        for(int i=0;i<sg::city_count;++i)cities[i]={6+sg::city_defs[i].x*225/1200,19+sg::city_defs[i].y*height/950};
        for(int y=19;y<19+height;++y)for(int x=0;x<240;++x) {
            const auto p=sg::overview_army_marker(x,y,cities,sg::city_count,19,19+height);
            if(!(p.x>=6 && p.x<=233 && p.y>=21 && p.y<=19+height-3)) {
                std::fprintf(stderr,"Army marker has no safe slot at %d,%d, height=%d\n",x,y,height);
                return 1;
            }
            // Larger pennants need a wider city-avoidance search in dense areas.
            assert(p.x-x>=-30 && p.x-x<=30 && p.y-y>=-30 && p.y-y<=30);
            for(const auto& city:cities) {
                const int dx=p.x-city.x,dy=p.y-city.y;
                if(!(dx<-8 || dx>8 || dy<-6 || dy>9)) {
                    std::fprintf(stderr,"Army marker still overlaps city at %d,%d, height=%d\n",x,y,height);
                    return 1;
                }
            }
            ++checked;
        }
        for(int y=20;y<18+height;++y)for(int x=3;x<=236;++x) {
            const auto flag=sg::map_flag_origin(x,y,19);
            assert(flag.x>=5 && flag.x+sg::map_flag_width-1<=235);
            assert(flag.y>=19 && flag.y+sg::map_flag_height<=19+height);
        }
    }
    std::printf("PASS map markers: %d army positions avoid every city; flag geometry stays inside both layouts\n",checked);
}
