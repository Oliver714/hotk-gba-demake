#include "render.h"
#include "magic_m012_projectile.h"
#include "magic_m012_visuals.h"

namespace sg {

void Render::magic_m012_frame(int script,int frame,int x,int y) {
    y-=12; // Aim at the mounted general's torso, not the foot-line/top-left.

    // These blitters use w as the source row stride; they do not resize.
    // Draw the complete imported art, independently of native collision extents.
    // Small widths here used to reinterpret 48px rows and slice away the glow.
    // Render companions first so the weapon's gold outline remains readable.
    const int wave_frame=(frame/2)&1;
    if(script==2660) {
        effect_white_light_rows(x-magic_m012_wave_a_x_offset[wave_frame],
                  y-magic_m012_wave_a_height/2,
                  magic_m012_wave_a_width,magic_m012_wave_a_height,
                  magic_m012_wave_a_sprites[wave_frame],
                  magic_m012_wave_a_row_spans[wave_frame]);
    } else {
        effect_white_light_rows(x-magic_m012_wave_d_x_offset[wave_frame],
                  y-magic_m012_wave_d_height/2,
                  magic_m012_wave_d_width,magic_m012_wave_d_height,
                  magic_m012_wave_d_sprites[wave_frame],
                  magic_m012_wave_d_row_spans[wave_frame]);
        if(script==2635) {
            // Sequence4205 cycles C6,C1-C5 at #4 with the half-light mixer.
            const int wave_c_frame=(frame/4)%6;
            effect_white_light_rows(x-magic_m012_wave_c_x_offset[wave_c_frame],
                      y-magic_m012_wave_c_height/2,
                      magic_m012_wave_c_width,magic_m012_wave_c_height,
                      magic_m012_wave_c_sprites[wave_c_frame],
                      magic_m012_wave_c_row_spans[wave_c_frame],true);
        }
    }

    if(script==2634) {
        const int xoff=magic_m012_main_b_x_offset[0];
        effect_white_light_rows(x-xoff,
                  y-magic_m012_main_b_height/2,
                  magic_m012_main_b_width,magic_m012_main_b_height,
                  magic_m012_main_b_sprites[0],magic_m012_main_b_row_spans[0]);
    } else if(script==2635) {
        const int main_frame=(frame/4)%6; // 4204: C6, then C1-C5.
        const int xoff=magic_m012_main_c_x_offset[main_frame];
        character(x-xoff,
                  y-magic_m012_main_c_height/2,
                  magic_m012_main_c_width,magic_m012_main_c_height,
                  magic_m012_main_c_sprites[main_frame],false);
    } else {
        const int xoff=magic_m012_projectile_x_offset;
        character(x-xoff,
                  y-magic_m012_projectile_height/2,
                  magic_m012_projectile_width,magic_m012_projectile_height,
                  magic_m012_projectile_sprite,false);
    }

}

}
