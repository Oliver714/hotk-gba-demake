#ifndef SANGO_MUSIC_ADPCM_H
#define SANGO_MUSIC_ADPCM_H
#include <stdint.h>
namespace sg::audio {
struct AdpcmTrack { const uint8_t* data; int blocks; };
extern const AdpcmTrack world_music,battle_music;
extern const uint32_t adpcm_transitions[89*16];
// Independent blocks permit bounded seeks and prevent predictor drift at loops.
struct MusicDecoder {
    const AdpcmTrack* song=nullptr;
    int block=0,sample=0,predictor=0,index=0;
    void seek(const AdpcmTrack& track,int position) { song=&track; block=position%track.blocks; sample=0; }
    int next() {
        const uint8_t* data=song->data+block*260;
        if(!sample) {
            predictor=int16_t(unsigned(data[0])|(unsigned(data[1])<<8)); index=data[2];
        } else {
            const int n=sample-1;
            const int code=(data[4+(n>>1)]>>((n&1)*4))&15;
            const uint32_t entry=adpcm_transitions[index*16+code];
            predictor+=int32_t(entry<<8)>>8;
            if(predictor>32767) predictor=32767;
            if(predictor<-32768) predictor=-32768;
            index=entry>>24;
        }
        if(++sample==512) { sample=0; if(++block==song->blocks) block=0; }
        return predictor;
    }
    void fill_block(int8_t* __restrict output,int volume);
};
}
#endif
