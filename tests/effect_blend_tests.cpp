#include "effect_blend.h"
#include "additive_palette.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
int main() {
    const uint8_t* table=&sg::additive_palette[0][0][0];
    for(int color=0;color<48;++color) assert(table[color*256+118]==118);
    for(int background=0;background<256;++background) assert(table[background]==118);
    for(int count=1;count<=8;++count) for(int color=0;color<256;++color)
        for(int background=0;background<256;++background) {
            uint8_t output[10],source[8];
            for(auto& p:output) p=uint8_t(background);
            for(auto& p:source) p=uint8_t(color);
            sg::blend_effect_span(output+1,source,count,table);
            const int expected=color>=118 && color<166 ? table[(color-118)*256+background] : color ? color : background;
            assert(output[0]==background && output[count+1]==background);
            for(int i=1;i<=count;++i) assert(output[i]==expected);
        }
    for(int mix=0;mix<2;++mix) for(int color=0;color<256;++color) {
        uint8_t source[512];
        for(int i=0;i<512;++i) source[i]=uint8_t(color+i*17);
        const uint8_t* lookup=&sg::additive_palette[mix][0][0];
        for(int step: {1,-1,2,-2,4,-4,8,-8,32,-32})
            for(int count=1;count<=8;++count) for(int background=0;background<256;++background) {
                uint8_t output[10];for(auto& p:output)p=uint8_t(background);
                sg::blend_effect_strided_span(output+1,source+256,count,lookup,step);
                assert(output[0]==background && output[count+1]==background);
                for(int i=0;i<count;++i) {
                    const int src=source[256+i*step];
                    const int expected=src>=118 && src<166 ? lookup[(src-118)*256+background] : src ? src : background;
                    assert(output[i+1]==expected);
                }
            }
    }
    sg::blend_effect_strided_span(nullptr,nullptr,0,nullptr,1);
    std::puts("PASS: 524288 exhaustive span cases match original palette/alpha semantics; no tile-row overrun");
    std::puts("PASS: 10485760 mixed-color strided cases, forward/reverse/zoom/full/half light; no tile-row overrun");
}
