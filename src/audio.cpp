#include "audio.h"
#include "music_adpcm.h"
#include "../vendor/butano/butano/hw/include/bn_hw_irq.h"
extern "C" volatile uint32_t sg_probe[];
extern "C" { volatile uint32_t sg_audio_probe[8]={}; }
extern "C" { alignas(4) BN_DATA_EWRAM int8_t sg_audio_ring[1280]; }

// One4bit ADPCM copy per song. DMA1 feeds a1024-byte PCM ring. Timer0 clocks
// samples, Timer1 cascades them and interrupts every512 samples. Unlike per-
// FIFO IRQs, this tolerates a long graphics VBlank IRQ without losing events.
// A256-byte mirrored guard covers FIFO prefetch while the ring IRQ is delayed.
// Graphics retain DMA0/3; the profiler retains Timer2/3.
namespace sg::audio {
void clock_tick();
void stream_tick();
namespace {
volatile uint16_t& reg(unsigned offset) {
    return *reinterpret_cast<volatile uint16_t*>(0x04000000u+offset);
}
volatile uint32_t& reg32(unsigned offset) {
    return *reinterpret_cast<volatile uint32_t*>(0x04000000u+offset);
}
constexpr uint16_t pitch[24]={
    1547,1575,1602,1627,1651,1673,1694,1714,1733,1751,1768,1784,
    1798,1812,1825,1838,1850,1861,1872,1882,1891,1900,1908,1916
};
volatile Scene requested_scene=Scene::menu;
volatile uint8_t master_volume=3,hit_wait=0;
volatile bool music_on=true,effects_on=true;
int active_track=-1,played_samples=0,saved_blocks[2]={0,0},fill_half=0,clock_samples=0;
BN_DATA_EWRAM MusicDecoder decoder;
struct AudioLock {
    uint16_t previous=reg(0x208);
    AudioLock() { reg(0x208)=0; }
    ~AudioLock() { reg(0x208)=previous; }
};
const AdpcmTrack& track(int id) { return id ? battle_music : world_music; }
void stop_pcm() {
    reg(0xc6)=0;
    reg(0x82)=0x0801; // Reset FIFO A, disable PCM routing, retain PSG50% SFX.
    if(active_track>=0) saved_blocks[active_track]=played_samples/512;
    active_track=-1;
    sg_audio_probe[0]=uint32_t(-1); sg_audio_probe[4]=0;
}
void start_pcm(int id,int block,int volume) {
    stop_pcm();
    reg(0x102)=0; reg(0x106)=0;
    const auto& song=track(id);
    block%=song.blocks;
    decoder.seek(song,block);
    decoder.fill_block(sg_audio_ring,volume);
    decoder.fill_block(sg_audio_ring+512,volume);
    for(int i=0;i<256;++i) sg_audio_ring[1024+i]=sg_audio_ring[i];
    active_track=id; played_samples=block*512; fill_half=0;
    reg32(0xc0)=0x040000a0;
    reg(0xc4)=4;
    reg(0x82)=0x0b05; // Reset, A to L+R, Timer0, PCM full volume.
    // Prefill all32 FIFO bytes manually: no back-to-back initial DMA requests.
    const auto* words=reinterpret_cast<const uint32_t*>(sg_audio_ring);
    for(int i=0;i<8;++i) reg32(0xa0)=words[i];
    reg32(0xbc)=uint32_t(sg_audio_ring+32);
    reg(0xc6)=0xb640;
    reg(0x100)=uint16_t(65536-1056);
    reg(0x104)=uint16_t(65536-512);
    reg(0x106)=0x00c4; // Count512 Timer0 sample overflows, then interrupt.
    reg(0x102)=0x0080;
    sg_audio_probe[0]=id; sg_audio_probe[1]=played_samples; sg_audio_probe[4]=volume;
    sg_audio_probe[6]=sg_audio_probe[6]+1;
}
void square1(int note,int volume,int length) {
    reg(0x60)=0;
    reg(0x62)=uint16_t((volume<<12)|0x0200|0x0080|(64-length));
    reg(0x64)=uint16_t(0xc000|pitch[note]);
}
}
void stream_tick() {
    if(active_track<0) return;
    const int offset=fill_half*512;
    if(fill_half==1) {
        // The timer counts actual played samples even if a higher-priority
        // graphics IRQ delayed us. Preserve queued FIFO samples and resume
        // beyond those already prefetched from the mirrored guard.
        reg(0xc6)=0;
        const int lag=int(reg(0x104))-(65536-512);
        if(unsigned(lag)>sg_audio_probe[7]) sg_audio_probe[7]=lag;
        reg32(0xbc)=uint32_t(sg_audio_ring+32+(lag&~15));
        reg(0xc4)=4;
        reg(0xc6)=0xb640;
    }
    const int volume=master_volume;
    decoder.fill_block(sg_audio_ring+offset,volume);
    if(!offset) for(int i=0;i<256;++i) sg_audio_ring[1024+i]=sg_audio_ring[i];
    fill_half^=1;
    played_samples+=512;
    if(played_samples>=track(active_track).blocks*512) {
        played_samples=0;
        sg_audio_probe[2]=sg_audio_probe[2]+1;
    }
    sg_audio_probe[1]=played_samples;
    sg_audio_probe[4]=volume;
    sg_audio_probe[5]=sg_audio_probe[5]+1;
}
void init() {
    reg(0x84)=0x0080;
    stop_pcm();
    reg(0x70)=0; reg(0x62)=0; reg(0x68)=0; reg(0x78)=0;
    set_volume(master_volume);
    reg(0x102)=0; reg(0x106)=0;
    bn::hw::irq::set_isr(bn::hw::irq::id::TIMER1,clock_tick);
    bn::hw::irq::enable(bn::hw::irq::id::TIMER1);
    reg(0x100)=uint16_t(65536-1056);
    reg(0x104)=uint16_t(65536-512);
    reg(0x106)=0x00c4; reg(0x102)=0x0080;
}
void play(Cue cue) {
    AudioLock lock;
    if(!effects_on || !master_volume) return;
    switch(cue) {
    case Cue::move: square1(19,4,6); break;
    case Cue::accept: square1(21,7,18); break;
    case Cue::back: square1(7,5,14); break;
    case Cue::magic: square1(23,9,42); reg(0x60)=0x0022; break;
    case Cue::hit:
        if(hit_wait) break;
        reg(0x78)=0x6200|56; reg(0x7c)=0xc025; hit_wait=8; break;
    default: break;
    }
}
void update(Scene scene,int elapsed_frames) {
    (void)elapsed_frames; requested_scene=scene;
}
void clock_tick() {
    clock_samples+=512;
    const int frames=clock_samples/266;
    clock_samples%=266;
    sg_probe[129]=sg_probe[129]+frames;
    hit_wait=hit_wait>frames ? hit_wait-frames : 0;
    if(!music_on || !master_volume) {
        if(active_track>=0) stop_pcm();
        return;
    }
    const int desired=requested_scene==Scene::battle ? 1 : 0;
    if(active_track!=desired) {
        start_pcm(desired,saved_blocks[desired],master_volume);
        sg_audio_probe[3]=sg_audio_probe[3]+1;
    } else stream_tick();
    // Existing probes now count PCM service frames, not PSG notes.
    sg_probe[130]=sg_probe[130]+1;
    if(sg_probe[131]<1) sg_probe[131]=1;
}
int volume() { return master_volume; }
void set_volume(int value) {
    AudioLock lock;
    master_volume=uint8_t(value<0 ? 0 : value>4 ? 4 : value);
    const int level=master_volume ? master_volume*2-1 : 0;
    reg(0x80)=master_volume ? uint16_t(0xbb00|level|(level<<4)) : 0;
    if(!master_volume && active_track>=0) stop_pcm();
}
bool music_enabled() { return music_on; }
bool effects_enabled() { return effects_on; }
void set_music_enabled(bool enabled) {
    AudioLock lock; music_on=enabled;
    if(!enabled) stop_pcm(); // PSG effects remain independent.
}
void set_effects_enabled(bool enabled) {
    AudioLock lock; effects_on=enabled;
    if(!enabled) { reg(0x62)=0; reg(0x78)=0; }
}
}
