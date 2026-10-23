#include "save_slot_ui.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    char out[24];
    sg::ui::slot_date_label(out,189,1);assert(std::strcmp(out,"189年1月")==0);
    sg::ui::slot_date_label(out,1000,12);assert(std::strcmp(out,"1000年12月")==0);
    sg::ui::slot_date_label(out,65535,12);assert(std::strcmp(out,"65535年12月")==0);
    sg::ui::slot_date_label(out,0,0);assert(std::strcmp(out,"0年0月")==0);
    for(unsigned year=0;year<=65535;++year) {
        for(unsigned month=1;month<=12;++month) {
            sg::ui::slot_date_label(out,uint16_t(year),uint8_t(month));
            char expected[24];std::snprintf(expected,sizeof(expected),"%u年%u月",year,month);
            assert(std::strcmp(out,expected)==0);
        }
    }
    puts("PASS: all uint16 years and 12 months format without overlap or truncation");
}
