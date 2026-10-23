#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include "battle_visual_rules.h"
#include "projectile_arrow_art.h"
#include "magic_lightning.h"
#include "magic_strikes.h"
#include "battle_camera.h"
#include <initializer_list>
int main() {
    {
        static sg::Battle battle{};
        int focus=500;
        assert(!sg::visual::lightning_focus(battle,focus) && focus==500);
        auto& p=battle.projectiles[0];
        p.active=1;p.magic=6;p.source_script=2624;p.target_side=1;p.x=800;
        assert(sg::visual::lightning_focus(battle,focus) && focus==800);
        p.phase=1;
        assert(sg::visual::lightning_focus(battle,focus) && focus==800);
        p.magic=9;p.source_script=2626;battle.sides[1].x=900;
        assert(sg::visual::lightning_focus(battle,focus) && focus==900);
        p.active=0;
        assert(!sg::visual::lightning_focus(battle,focus));
    }
    for(int index=0;index<6;++index) {
        const int dx=index%2 ? 1 : -1,dy=index<2 ? -1 : index<4 ? 1 : 0;
        const auto& glyph=sg::visual::projectile_arrows[index];
        assert(glyph.pixels[(7+dy*5)*16+7+dx*5]==1);
        int head_projection=0;
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) if(glyph.pixels[y*16+x]==1)
            head_projection+=(x-7)*dx+(y-7)*dy;
        assert(head_projection>0);
    }
    for(int ground=-100;ground<500;++ground) {
        assert(sg::visual::grounded_top(ground,46)+46==ground+15);
        assert(sg::visual::grounded_top(ground,48)+48==ground+15);
        assert(sg::visual::centered_left(ground,72)+36==ground);
        // All lightning families share a bottom-center target anchor, also
        // used by full-field fit bounds; they must not extend below the feet.
        for(int height: {sg::magic_lightning_height,sg::magic_strike_height}) {
            assert(sg::visual::grounded_top(ground,height)+height==ground+sg::visual::commander_feet);
            assert(sg::visual::grounded_top(ground,height)<ground);
        }
        assert(sg::visual::centered_left(ground,sg::magic_lightning_width)+sg::magic_lightning_width/2==ground);
    }
    for(int id: {71,81}) for(int side=0;side<2;++side) {
        prepare_skill_review(id,side,100);
        bool saw_clone=false;
        for(int tick=0;tick<250;++tick) {
            game.battle_tick();
            for(const auto& p:game.battle.projectiles) if(p.active && p.magic==1 &&
                (p.source_script==2646 || p.source_script==2664)) {
                saw_clone=true;
                assert(p.y==sg::battle_commander_y);
            }
        }
        assert(saw_clone);
    }
    for(int side=0;side<2;++side) {
        prepare_skill_review(74,side,0);
        const int center_x=game.battle.magic_field_origin_x,center_y=game.battle.magic_field_origin_y;
        bool seen[5]={};
        constexpr int expected_x[5]={0,80,-80,0,0},expected_y[5]={0,0,0,-30,30};
        for(int tick=0;tick<150;++tick) {
            game.battle_tick();
            for(const auto& p:game.battle.projectiles) if(p.active && p.magic==13 && p.phase==1 && p.kind==0) {
                bool matched=false;
                for(int i=0;i<5;++i) if(p.x-center_x==expected_x[i] && p.y-center_y==expected_y[i])
                    seen[i]=matched=true;
                assert(matched);
            }
        }
        for(bool hit:seen) assert(hit);
    }
    std::puts("PASS: shared grounded/centered anchors and clone collision lanes for both sides");
}
