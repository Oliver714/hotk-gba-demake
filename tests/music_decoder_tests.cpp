#include "music_adpcm.h"
#include <cassert>
#include <cstdio>
#include <vector>
extern "C" const uint8_t music_world_adpcm[]={0},music_battle_adpcm[]={0};
int main(int argc,char** argv) {
    assert(argc==3);
    FILE* in=std::fopen(argv[1],"rb"); assert(in);
    std::fseek(in,0,SEEK_END); const int size=std::ftell(in); std::rewind(in);
    assert(size%260==0);
    std::vector<uint8_t> data(size); assert(std::fread(data.data(),1,size,in)==unsigned(size)); std::fclose(in);
    const sg::audio::AdpcmTrack track{data.data(),size/260};
    sg::audio::MusicDecoder decoder; decoder.seek(track,0);
    FILE* out=std::fopen(argv[2],"wb"); assert(out);
    std::vector<int16_t> decoded(track.blocks*512);
    for(auto& sample:decoded) { sample=decoder.next(); std::fwrite(&sample,2,1,out); }
    std::fclose(out);
    for(int i=0;i<track.blocks*512;++i) assert(decoder.next()==decoded[i]);
    decoder.seek(track,0);
    int8_t small[512];
    for(int block=0;block<track.blocks;++block) {
        decoder.fill_block(small,3);
        for(int i=0;i<512;++i) assert(small[i]==int8_t((int(decoded[block*512+i])*3)>>10));
    }
    for(int block=0;block<track.blocks;block+=17) {
        decoder.seek(track,block);
        for(int i=0;i<512;++i) assert(decoder.next()==decoded[block*512+i]);
    }
    std::puts("PASS: complete decode, loop and block seeks");
}
