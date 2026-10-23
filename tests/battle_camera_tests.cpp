#include "battle_camera.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    for(unsigned seed=1;seed<=32;++seed) {
        static sg::Battle battle;
        battle={};
        unsigned state=seed;
        auto random=[&]() {state=state*1664525u+1013904223u;return state;};
        const int magic[8]={1,5,6,9,23,36,0,20};
        const int scripts[8]={2608,2611,2642,2624,2625,2626,2617,2644};
        battle.sides[0].x=700;battle.sides[1].x=1500;
        for(auto& actor:battle.projectiles) {
            actor.active=uint8_t(random()%3!=0);actor.magic=uint8_t(magic[random()%8]);
            actor.source_script=int16_t(scripts[random()%8]);actor.kind=uint8_t(random()%4);
            actor.phase=uint8_t(random()%4);actor.remaining=uint8_t(random()%20);
            actor.owner_index=uint8_t(random()%4);actor.x=int16_t(random()%2240);
            actor.y=int16_t(random()%390);actor.height=int16_t(random()%390);
            actor.target_offset_x=int16_t(random()%2240);actor.target_side=int8_t(int(random()%4)-1);
        }
        const auto grouped=sg::visual::effect_camera_subjects(battle);
        int x=0,y=0;
        const bool fire=sg::visual::firewheel_focus(battle,x,y);
        assert(fire==bool(grouped.firewheel));
        if(fire) assert(x==grouped.firewheel->target_offset_x && y==grouped.firewheel->height);
        const bool cyclone=sg::visual::cyclone_focus(battle,x,y);
        assert(cyclone==bool(grouped.cyclone));
        if(cyclone) assert(x==grouped.cyclone->x && y==grouped.cyclone->y);
        const bool lightning=sg::visual::lightning_focus(battle,x);
        assert(lightning==bool(grouped.lightning_count));
        if(lightning) assert(x==grouped.lightning_sum/grouped.lightning_count);
    }
    sg::Projectile p{};p.active=1;p.x=1100;p.y=240;
    for(int magic: {10,24,34,35}) for(int phase=0;phase<5;++phase) {
        p.magic=uint8_t(magic);p.phase=uint8_t(phase);
        assert(!sg::visual::effect_visible(p) && !sg::visual::effect_frames_camera(p));
    }
    p.magic=20;p.source_script=2628;p.phase=1;p.kind=0;
    assert(!sg::visual::effect_visible(p));
    p.kind=1;p.height=300;
    assert(sg::visual::effect_frames_camera(p) && sg::visual::effect_frame_y(p)==240);
    p.phase=2;assert(!sg::visual::effect_frames_camera(p));
    p.magic=11;p.phase=2;assert(!sg::visual::effect_visible(p));
    p.phase=1;p.height=72;
    assert(sg::visual::effect_frames_camera(p) && sg::visual::effect_frame_y(p)==210);
    p.magic=2;p.height=100;assert(sg::visual::effect_frame_y(p)==140);
    p.magic=1;p.kind=0;p.phase=1;p.collision_enabled=1;
    assert(sg::visual::camera_tracks(p));
    p.phase=0;assert(!sg::visual::camera_tracks(p));
    p.phase=3;assert(!sg::visual::camera_tracks(p));
    p.phase=1;p.kind=2;assert(!sg::visual::camera_tracks(p));
    p.magic=21;p.kind=0;p.source_script=2663;
    assert(!sg::visual::camera_tracks(p) && !sg::visual::effect_frames_camera(p));
    p.magic=13;p.phase=2;assert(!sg::visual::effect_visible(p));
    p.source_script=2659;p.phase=1;p.kind=1;p.height=7;assert(!sg::visual::effect_visible(p));
    {
        static sg::Battle battle{};
        int x=0,y=0;
        auto& first=battle.projectiles[0];
        first.active=1;first.magic=5;first.phase=0;first.remaining=10;first.x=500;first.y=120;
        assert(!sg::visual::cyclone_focus(battle,x,y));
        first.remaining=9;
        assert(sg::visual::cyclone_focus(battle,x,y) && x==500 && y==120);
        first.phase=2;
        assert(sg::visual::effect_visible(first));
        auto& next=battle.projectiles[1];
        next=first;next.owner_index=1;next.x=800;next.y=240;
        assert(sg::visual::cyclone_focus(battle,x,y) && x==800 && y==240);
        first.active=next.active=0;
        assert(!sg::visual::cyclone_focus(battle,x,y));
    }
    std::puts("PASS: invisible schedulers/proxies, windup states, decorative particles, active subjects and real altitude");
}
