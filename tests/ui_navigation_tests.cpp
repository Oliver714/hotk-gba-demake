#include "ui_navigation.h"
#include <cassert>
#include <cstdio>
int main() {
    using sg::ui::repeat_direction;
    // Exact wall-frame action times at 60 Hz and regularly missed frames.
    for(int step=1;step<=4;++step) {
        uint8_t age=0;
        assert(repeat_direction(true,true,age,99));
        int total=1;
        for(int t=step;t<=120;t+=step) if(repeat_direction(false,true,age,step)) {
            const int expected=12+(total-1)*4;
            assert(t>=expected && t<expected+step);
            ++total;
        }
        assert(total==29);
        assert(!repeat_direction(false,false,age,1000) && age==0);
    }
    uint8_t age=0;
    assert(repeat_direction(true,true,age,1));
    assert(!repeat_direction(false,true,age,7));
    assert(repeat_direction(false,true,age,9)); // t=16: one action, not two.
    assert(!repeat_direction(false,true,age,1));
    assert(repeat_direction(false,true,age,1000)); // No byte overflow/backlog.
    assert(!repeat_direction(false,true,age,1));
    assert(repeat_direction(true,true,age,1000) && age==0);
    puts("PASS: hardware-frame repeat cadence at 1/2/3/4-frame polling; no catch-up burst; release/press reset");
    using sg::ui::page_step;
    assert(page_step(0,0,1)==0);
    for(int count=1;count<=256;++count) for(int i=0;i<count;++i) {
        const int left=page_step(i,count,-1),right=page_step(i,count,1);
        assert(left>=0 && left<=i && i-left<=5);
        assert(right>=i && right<count && right-i<=5);
        if(i>=5) assert(left==i-5);
        if(i+5<count) assert(right==i+5);
    }
    assert(page_step(4,6,1)==5);
    assert(page_step(5,6,1)==5);
    assert(page_step(0,12,-1)==0);
    assert(page_step(0,12,1)==5);
    assert(page_step(5,12,1)==10);
    puts("PASS: five-row paging, empty/short/partial lists and both boundaries (0..256)");
}
