#ifndef SANGO_PROJECTILE_ARROW_ART_H
#define SANGO_PROJECTILE_ARROW_ART_H
#include <stdint.h>
namespace sg::visual {
struct ArrowGlyph { uint8_t pixels[256]={}; };
constexpr void arrow_line(ArrowGlyph& glyph,int x,int y,int end_x,int end_y,uint8_t ink) {
    const int dx=end_x>x ? end_x-x : x-end_x,dy=end_y>y ? end_y-y : y-end_y;
    const int sx=x<end_x ? 1 : -1,sy=y<end_y ? 1 : -1;
    int error=dx-dy;
    for(;;) {
        if(x>=0 && x<16 && y>=0 && y<16) glyph.pixels[y*16+x]=ink;
        if(x==end_x && y==end_y) break;
        const int twice=2*error;
        if(twice>-dy) { error-=dy; x+=sx; }
        if(twice<dx) { error+=dx; y+=sy; }
    }
}
constexpr ArrowGlyph make_arrow(int dx,int dy) {
    ArrowGlyph glyph;
    const int tail_x=7-dx*5,tail_y=7-dy*5,tip_x=7+dx*5,tip_y=7+dy*5;
    arrow_line(glyph,tail_x,tail_y+1,tip_x,tip_y+1,6);
    arrow_line(glyph,tail_x,tail_y,tip_x,tip_y,2);
    arrow_line(glyph,tip_x,tip_y,tip_x-dx*3-dy*2,tip_y-dy*3+dx*2,1);
    arrow_line(glyph,tip_x,tip_y,tip_x-dx*3+dy*2,tip_y-dy*3-dx*2,1);
    arrow_line(glyph,tail_x,tail_y,tail_x+dx*2-dy*2,tail_y+dy*2+dx*2,11);
    arrow_line(glyph,tail_x,tail_y,tail_x+dx*2+dy*2,tail_y+dy*2-dx*2,11);
    return glyph;
}
// Compile native pixel art into ROM once; never draw six lines per arrow per frame.
alignas(4) inline constexpr ArrowGlyph projectile_arrows[6]={
    make_arrow(-1,-1),make_arrow(1,-1),make_arrow(-1,1),
    make_arrow(1,1),make_arrow(-1,0),make_arrow(1,0)
};
}
#endif
