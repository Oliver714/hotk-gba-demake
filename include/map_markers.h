#ifndef SANGO_MAP_MARKERS_H
#define SANGO_MAP_MARKERS_H
namespace sg {
struct MapMarkerPoint { int x,y; };
struct MapMarkerRect { int left,top,right,bottom; };
inline bool map_marker_intersects(const MapMarkerRect& a,const MapMarkerRect& b) {
    return a.left<=b.right && a.right>=b.left && a.top<=b.bottom && a.bottom>=b.top;
}
inline MapMarkerRect army_marker_bounds(MapMarkerPoint p,bool focused) {
    return focused ? MapMarkerRect{p.x-6,p.y-8,p.x+7,p.y+5} :
        MapMarkerRect{p.x-4,p.y-6,p.x+5,p.y+3};
}
inline bool zoom_army_marker(int x,int y,const MapMarkerRect* occupied,int count,int top,int bottom,
                             bool focused,MapMarkerPoint& result) {
    auto available=[&](int cx,int cy) {
        result={cx<10 ? 10 : cx>228 ? 228 : cx,cy<top+9 ? top+9 : cy>bottom-7 ? bottom-7 : cy};
        const auto bounds=army_marker_bounds(result,focused);
        for(int i=0;i<count;++i) if(map_marker_intersects(bounds,occupied[i])) return false;
        return true;
    };
    if(available(x,y)) return true;
    for(int radius=12;radius<=96;radius+=12) for(int d=-radius;d<=radius;d+=12)
        if(available(x+d,y-radius) || available(x+d,y+radius) ||
           available(x-radius,y+d) || available(x+radius,y+d)) return true;
    // Dense deployments near a city may exhaust the local ring. Search the
    // viewport, never fall back to painting over a city's ownership flag.
    for(int cy=top+9;cy<bottom-6;cy+=4) for(int cx=10;cx<=228;cx+=4)
        if(available(cx,cy)) return true;
    return false;
}
inline bool map_army_label(MapMarkerPoint marker,const MapMarkerRect* occupied,int count,int top,int bottom,
                           MapMarkerPoint& result) {
    const MapMarkerPoint options[]={{marker.x-20,marker.y+8},{marker.x-20,marker.y-23},
        {marker.x+14,marker.y-5},{marker.x-68,marker.y-5}};
    for(const auto& p:options) {
        result={p.x<5 ? 5 : p.x>175 ? 175 : p.x,p.y<top+1 ? top+1 : p.y>bottom-14 ? bottom-14 : p.y};
        const MapMarkerRect bounds={result.x-1,result.y-1,result.x+58,result.y+12};
        bool clear=true;
        for(int i=0;i<count;++i) if(map_marker_intersects(bounds,occupied[i])) { clear=false;break; }
        if(clear) return true;
    }
    // The bottom HUD already names the selected general. Omit only the
    // redundant floating label if no clear slot exists, instead of hiding a city.
    return false;
}
inline constexpr int map_flag_width=13, map_flag_height=11;
inline constexpr MapMarkerPoint map_flag_origin(int x,int y,int top) {
    const int left=x-4;
    return {left<5 ? 5 : left>223 ? 223 : left, y-15>=top ? y-15 : y+6};
}
// Keep marching tokens visible, but do not let them impersonate/cover a city.
inline MapMarkerPoint overview_army_marker(int x,int y,const MapMarkerPoint* cities,int count,int top,int bottom) {
    constexpr MapMarkerPoint offsets[]={{0,0},{0,-8},{0,8},{-8,0},{8,0},{-8,-8},{8,-8},{-8,8},{8,8},
        {0,-16},{0,16},{-16,0},{16,0},{-16,-16},{16,-16},{-16,16},{16,16},
        {-8,-16},{8,-16},{-8,16},{8,16},{-16,-8},{16,-8},{-16,8},{16,8},
        {0,-24},{0,24},{-24,0},{24,0},{-24,-24},{24,-24},{-24,24},{24,24},
        {-8,-24},{8,-24},{-8,24},{8,24},{-24,-8},{24,-8},{-24,8},{24,8},
        {-16,-24},{16,-24},{-16,24},{16,24},{-24,-16},{24,-16},{-24,16},{24,16}};
    for(const auto& offset:offsets) {
        MapMarkerPoint candidate={x+offset.x,y+offset.y};
        if(candidate.x<6)candidate.x=6;
        if(candidate.x>233)candidate.x=233;
        if(candidate.y<top+6)candidate.y=top+6;
        if(candidate.y>bottom-4)candidate.y=bottom-4;
        bool overlaps=false;
        for(int i=0;i<count;++i) {
            const int dx=candidate.x-cities[i].x,dy=candidate.y-cities[i].y;
            if(dx>=-8 && dx<=8 && dy>=-6 && dy<=9) {overlaps=true;break;}
        }
        if(!overlaps)return candidate;
    }
    return {x,y};
}
}
#endif
