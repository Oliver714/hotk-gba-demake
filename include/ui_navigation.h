#ifndef SANGO_UI_NAVIGATION_H
#define SANGO_UI_NAVIGATION_H
#include <cstdint>
namespace sg::ui {
// Keep cadence in hardware frames; drop missed actions instead of a burst.
inline bool repeat_direction(bool pressed,bool held,uint8_t& counter,int elapsed) {
    if(pressed) { counter=0; return true; }
    if(!held) { counter=0; return false; }
    const unsigned age=unsigned(counter)+unsigned(elapsed>0 ? elapsed : 1);
    if(age>=12) { counter=uint8_t(8+(age-12)%4); return true; }
    counter=uint8_t(age);
    return false;
}
// Five visible rows. Shoulders never wrap unexpectedly at either list edge.
constexpr int page_step(int current,int count,int direction) {
    if(count<=0) return 0;
    const int next=current+direction*5;
    return next<0 ? 0 : next>=count ? count-1 : next;
}
}
#endif
