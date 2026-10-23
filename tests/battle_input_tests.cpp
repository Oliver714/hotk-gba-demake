#include "battle_input.h"
#include <cassert>
#include <cstdio>
int main() {
    sg::ui::BattleShoulders s;
    for(int first=0;first<2;++first) for(int gap=1;gap<=5;++gap) {
        s.reset(); assert(s.update(!first,first,!first,first,false,1)==0);
        assert(s.update(first,!first,true,true,false,gap)==2);
        for(int i=0;i<20;++i) assert(s.update(false,false,true,true,false,4)==0);
        assert(s.update(false,false,false,true,false,1)==0);
        assert(s.update(true,false,true,true,false,1)==0);
        assert(s.update(false,false,false,false,false,1)==0);
    }
    s.reset();assert(s.update(true,true,true,true,false,1)==2);
    s.reset();assert(s.update(true,false,true,false,false,1)==0);
    assert(s.update(false,false,false,false,false,1)==-1); // short tap
    s.reset();assert(s.update(false,true,false,true,false,1)==0);
    assert(s.update(false,false,false,true,false,6)==1);
    assert(s.update(false,false,false,true,false,1000)==0);
    s.reset();assert(s.update(false,true,false,true,false,1)==0);
    assert(s.update(false,false,false,true,true,3)==0);
    assert(s.update(false,false,false,true,false,20)==0); // pan release cannot select
    s.reset();assert(s.update(true,false,true,false,false,1)==0);
    assert(s.update(false,false,true,false,false,1000)==-1);
    s.reset();assert(s.update(true,false,true,false,false,1)==0);
    s.suspend(true,false); // Leaving a page cancels its pending single.
    assert(s.update(false,true,true,true,false,1)==0);
    assert(s.update(false,false,true,true,false,1000)==0);
    assert(s.update(false,false,false,false,false,1)==0);
    assert(s.update(true,true,true,true,false,1)==2);
    s.suspend(false,false); // A fresh press after release is not swallowed.
    assert(s.update(false,true,false,true,false,1)==0);
    assert(s.update(false,false,false,false,false,1)==1);
    puts("PASS: both stagger orders, simultaneous, held/repress suppression, tap, long hold, row-pan, long stall/reset, page suspension");
}
