#include "map_markers.h"
#include "data.h"
#include <cassert>
#include <cstdio>
int main() {
    int placements=0,labels=0;
    for(int camera=0;camera<sg::city_count;++camera) {
        sg::MapMarkerRect occupied[sg::city_count*2+sg::max_armies];int count=0;
        for(int c=0;c<sg::city_count;++c) {
            const int x=120+(int(sg::city_defs[c].x)-sg::city_defs[camera].x)*450/1200;
            const int y=75+(int(sg::city_defs[c].y)-sg::city_defs[camera].y)*192/950;
            if(x<3 || x>236 || y<20 || y>=130) continue;
            occupied[count++]={x-5,y-3,x+5,y+4};
            const auto f=sg::map_flag_origin(x,y,19);
            occupied[count++]={f.x-1,f.y<y-1 ? f.y : y-1,f.x+12,f.y+10>y+1 ? f.y+10 : y+1};
        }
        sg::MapMarkerPoint first{};
        for(int army=0;army<sg::max_armies;++army) {
            sg::MapMarkerPoint p;assert(sg::zoom_army_marker(120,75,occupied,count,19,131,army==0,p));
            const auto b=sg::army_marker_bounds(p,army==0);
            assert(b.left>=3 && b.right<=236 && b.top>=19 && b.bottom<131);
            for(int i=0;i<count;++i)
                assert(b.left>occupied[i].right || b.right<occupied[i].left || b.top>occupied[i].bottom || b.bottom<occupied[i].top);
            occupied[count++]=b;if(!army) first=p;++placements;
            if(!army) {
                sg::MapMarkerPoint p;
                if(sg::map_army_label(first,occupied,count,19,131,p)) {
                    const sg::MapMarkerRect label={p.x-1,p.y-1,p.x+58,p.y+12};
                    for(int i=0;i<count;++i) assert(label.left>occupied[i].right || label.right<occupied[i].left ||
                        label.top>occupied[i].bottom || label.bottom<occupied[i].top);
                    ++labels;
                }
            }
        }
        sg::MapMarkerPoint label;
        if(sg::map_army_label(first,occupied,count,19,131,label)) {
            const sg::MapMarkerRect b={label.x-1,label.y-1,label.x+58,label.y+12};
            for(int i=0;i<count;++i)
                assert(b.left>occupied[i].right || b.right<occupied[i].left || b.top>occupied[i].bottom || b.bottom<occupied[i].top);
            ++labels;
        }
    }
    assert(labels>0);
    std::printf("PASS %d max-density zoom markers preserve city gates/flags and each other; %d labels find clear slots\n",placements,labels);
}
