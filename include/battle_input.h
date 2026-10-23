#ifndef SANGO_BATTLE_INPUT_H
#define SANGO_BATTLE_INPUT_H
#include <cstdint>
namespace sg::ui {
// One shoulder gesture per release cycle. Give the second button six hardware
// frames to join before committing a standalone previous/next action.
struct ShoulderGesture {
    int8_t pending=0;
    uint8_t age=0;
    bool consumed=false;
    void reset() { pending=0; age=0; consumed=false; }
    void suspend(bool lh,bool rh) { pending=0; age=0; consumed=lh || rh; }
    // -1/1: standalone action; 2: chord; 0: no action.
    int update(bool lp,bool rp,bool lh,bool rh,bool row_pan,int elapsed) {
        if(consumed) { if(!lh && !rh) reset(); return 0; }
        if(lh && rh) { pending=0; consumed=true; return 2; }
        if(row_pan) { pending=0; consumed=true; return 0; }
        if(pending) {
            const int time=int(age)+(elapsed>0 ? elapsed : 1);
            if(!(pending<0 ? lh : rh) || time>=6) {
                const int result=pending;
                pending=0; consumed=lh || rh; return result;
            }
            age=uint8_t(time);
        }
        if(lp || rp) { pending=lp ? -1 : 1; age=0; }
        return 0;
    }
};
using BattleShoulders=ShoulderGesture;
}
#endif
