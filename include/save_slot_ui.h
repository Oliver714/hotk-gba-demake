#ifndef SANGO_SAVE_SLOT_UI_H
#define SANGO_SAVE_SLOT_UI_H
#include <cstdint>
namespace sg::ui {
inline char* append_slot_number(char* out,unsigned value) {
    char digits[5];int end=5;
    do {digits[--end]=char('0'+value%10);value/=10;} while(value);
    while(end<5)*out++=digits[end++];
    return out;
}
inline void slot_date_label(char (&out)[24],uint16_t year,uint8_t month) {
    char* cursor=append_slot_number(out,year);
    const char* text="年";while(*text)*cursor++=*text++;
    cursor=append_slot_number(cursor,month);
    text="月";while(*text)*cursor++=*text++;
    *cursor=0;
}
}
#endif
