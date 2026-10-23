#include "music_adpcm.h"
namespace sg::audio {
// Out-of-line/non-cloned: LTO must not move the hot loop back into ROM.
#ifdef __arm__
__attribute__((target("arm"),section(".iwram"),noipa))
#endif
void MusicDecoder::fill_block(int8_t* __restrict output,int volume) {
    const uint8_t* bytes=song->data+block*260;
    int value=int16_t(unsigned(bytes[0])|(unsigned(bytes[1])<<8));
    int state=bytes[2];
    output[0]=int8_t((value*volume)>>10);
    auto emit=[&](int code,int at) {
        const uint32_t entry=adpcm_transitions[state*16+code];
        value+=int32_t(entry<<8)>>8;
        if(value>32767) value=32767;
        if(value<-32768) value=-32768;
        state=entry>>24;
        output[at]=int8_t((value*volume)>>10);
    };
    for(int i=0;i<255;++i) {
        const int codes=bytes[4+i];
        emit(codes&15,i*2+1); emit(codes>>4,i*2+2);
    }
    emit(bytes[259]&15,511);
    if(++block==song->blocks) block=0;
    sample=0;
}
}
