#include "effect_blend.h"
namespace sg {
// Reverse/zoom/half-light path with identical LUT semantics. A tile-row
// segment avoids recomputing the tiled framebuffer address for every pixel.
#ifdef __arm__
__attribute__((target("arm"),section(".iwram"),noipa))
#endif
void blend_effect_strided_span(uint8_t* output,const uint8_t* source,int count,const uint8_t* table,int step) {
    while(count>0) {
        const unsigned color=*source;
        if(color>=118 && color<166) *output=table[(color-118)*256+*output];
        else if(color) *output=uint8_t(color);
        ++output;
        if(!--count) break;
        source+=step;
    }
}
// Small ARM hot loop: do not spend scarce IWRAM on clipping or sprite setup.
// Output is a contiguous section of a single framebuffer tile row.
#ifdef __arm__
__attribute__((target("arm"),section(".iwram"),noipa))
#endif
void blend_effect_span(uint8_t* output,const uint8_t* source,int count,const uint8_t* table) {
    do {
        const unsigned color=*source++;
        if(color>=118 && color<166) {
            // Palette118 is the verified saturated fixed point of every
            // full-light row; its own source row also maps every color118.
            // Dense overlaps need no further ROM lookup or EWRAM write.
            if(*output!=118) *output=color==118 ? 118 : table[(color-118)*256+*output];
        }
        else if(color) *output=uint8_t(color);
        ++output;
    } while(--count);
}
}
