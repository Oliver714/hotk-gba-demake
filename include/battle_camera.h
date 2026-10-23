#ifndef SANGO_BATTLE_CAMERA_H
#define SANGO_BATTLE_CAMERA_H
#include "model.h"
namespace sg::visual {
struct EffectCameraSubjects {
    const Projectile* firewheel=nullptr;
    const Projectile* cyclone=nullptr;
    const Projectile* dragon=nullptr;
    const Projectile* ambush=nullptr;
    const Projectile* yellow=nullptr;
    int lightning_sum=0,lightning_count=0;
};
inline EffectCameraSubjects effect_camera_subjects(const Battle& battle) {
    EffectCameraSubjects result;
    for(const auto& p:battle.projectiles) {
        if(!p.active) continue;
        if(p.magic==1) {
            if(!result.firewheel && (p.kind==2 || p.kind==3) &&
               (p.source_script==2608 || p.source_script==2611)) result.firewheel=&p;
            if(!result.dragon && p.source_script>=2642 && p.source_script<=2644) result.dragon=&p;
        } else if(p.magic==5) {
            if((p.phase==2 || (p.phase==0 && p.remaining<=9)) &&
               (!result.cyclone || p.owner_index>result.cyclone->owner_index)) result.cyclone=&p;
        } else if(p.magic==36 && p.phase==1 && p.kind && !result.ambush) result.ambush=&p;
        else if(p.magic==23 && p.source_script==2617 && !result.yellow) result.yellow=&p;
        if(p.target_side>=0 && p.target_side<2) {
            if(p.magic==6 && (p.source_script==2624 || p.source_script==2625)) {
                result.lightning_sum+=p.x;++result.lightning_count;
            } else if(p.magic==9 && p.source_script==2626) {
                result.lightning_sum+=battle.sides[int(p.target_side)].x;++result.lightning_count;
            }
        }
    }
    return result;
}
inline bool firewheel_focus(const Battle& battle,int& x,int& y) {
    for(const auto& p:battle.projectiles)
        if(p.active && p.magic==1 && (p.kind==2 || p.kind==3) &&
           (p.source_script==2608 || p.source_script==2611)) {
            x=p.target_offset_x;y=p.height;return true;
        }
    return false;
}
inline bool cyclone_focus(const Battle& battle,int& x,int& y) {
    const Projectile* selected=nullptr;
    for(const auto& p:battle.projectiles)
        if(p.active && p.magic==5 && (p.phase==2 || (p.phase==0 && p.remaining<=9)) &&
           (!selected || p.owner_index>selected->owner_index)) selected=&p;
    if(!selected) return false;
    x=selected->x;y=selected->y;
    return true;
}
inline bool lightning_focus(const Battle& battle,int& x) {
    int sum=0,count=0;
    for(const auto& p:battle.projectiles) {
        if(!p.active || p.target_side<0 || p.target_side>1) continue;
        if(p.magic==6 && (p.source_script==2624 || p.source_script==2625)) {
            sum+=p.x; ++count;
        } else if(p.magic==9 && p.source_script==2626) {
            sum+=battle.sides[int(p.target_side)].x; ++count;
        }
    }
    if(!count) return false;
    x=sum/count;
    return true;
}
inline bool camera_tracks(const Projectile& p) {
    return p.active && p.magic==1 && p.phase==1 && p.kind<2 && p.collision_enabled;
}
// One visibility decision for drawing and framing: invisible VM timers and
// collision proxies must never fall through to the generic arrow renderer.
inline bool effect_visible(const Projectile& p) {
    if(!p.active) return false;
    switch(p.magic) {
    case 0: case 22: case 32: case 33: return true;
    case 1: return p.phase==1;
    case 2: case 9: case 11: case 12: case 14: case 16: case 18:
    case 23: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
        return p.phase==1 && (p.magic!=18 || p.height<10);
    case 3: return p.phase==3;
    case 4: return (p.phase>0 && p.phase<33) || (p.phase>=33 && !p.scatter);
    case 5: return p.phase==2;
    case 6: case 8: return p.phase!=0;
    case 7: return p.kind && p.phase;
    case 13:
        if(p.source_script==2665) return p.phase!=2;
        return p.phase==1 && (p.kind==0 || (p.kind==1 && p.height<=(p.source_script==2659 ? 6 : 4)));
    case 15: return p.phase==1 || (p.source_script!=2664 && p.phase==3 && (p.kind&2));
    case 17: return p.remaining && p.target_side>=0 && p.target_side<2;
    case 19: return p.phase>=1 && p.phase<=3;
    case 20: return p.kind==1 && p.phase==1 && (p.source_script==2627 || p.source_script==2628);
    case 21: return p.phase==1 && p.source_script==2663;
    // magic10 is only a collision cell; 24/34/35 are scheduler tokens.
    default: return false;
    }
}
inline bool effect_frames_camera(const Projectile& p) {
    if(!effect_visible(p) || p.x<0 || p.x>=battlefield_width) return false;
    switch(p.magic) {
    // Do not zoom out for orbit particles, hit flashes, flying debris,
    // permanent wind remnants, or the decorative ascending arrow volley.
    case 14: case 17: case 18: case 19: case 21: case 22: case 23:
    case 26: case 27: case 31: case 32: case 33: return false;
    default: return true;
    }
}
inline int effect_frame_y(const Projectile& p) {
    switch(p.magic) {
    case 2: case 3: case 16: case 25: return p.y-p.height;
    case 4: case 11: return p.y-p.height*battle_grid_row_height/72;
    case 13: return p.y-(p.source_script==2665 ? p.height*battle_grid_row_height/72 : 0);
    case 5: return p.y-24; // Grounded cyclone; height is its animation age.
    default: return p.y; // Cart height, for example, is animation age.
    }
}
}
#endif
