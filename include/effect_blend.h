#ifndef SANGO_EFFECT_BLEND_H
#define SANGO_EFFECT_BLEND_H
#include <stdint.h>
namespace sg {
void blend_effect_span(uint8_t* output,const uint8_t* source,int count,const uint8_t* table);
void blend_effect_strided_span(uint8_t* output,const uint8_t* source,int count,const uint8_t* table,int step);
}
#endif
