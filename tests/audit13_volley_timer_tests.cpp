#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include <initializer_list>
static int id(int script) {for(const auto& skill:sg::skill_defs) if(skill.script==script) return skill.id;assert(false);return 0;}
static sg::Projectile& arrow(int script) {
    for(auto& p:game.battle.projectiles) if(p.active && p.magic==2 && p.source_script==script) return p;
    assert(false);return game.battle.projectiles[0];
}
int main() {
    int failures=0;
    for(int side=0;side<2;++side) for(int script: {2600,2604}) {
        prepare_skill_review(id(script),side,1);
        for(int tick=0;tick<171;++tick) game.battle_tick();
        assert(!game.battle.effect_remaining);auto& p=arrow(script);
        assert(!p.phase && !p.kind && p.remaining>5);const int before=p.remaining;
        auto& enemy=game.battle.sides[1-side];enemy.mp=1000;enemy.charge=sg::original::cast_charge_max;
        game.campaign.officers[enemy.general].learned[0]=uint8_t(id(2650));
        assert(game.cast_skill(1-side,0));
        for(int tick=0;tick<5;++tick) game.battle_tick();
        failures+=p.remaining!=before-5;
    }
    for(int side=0;side<2;++side) {
        prepare_skill_review(id(2664),side,1);
        for(int tick=0;tick<90;++tick) game.battle_tick();
        failures+=arrow(2604).kind!=1;
    }
    std::printf("%s: volley setup/child timers must be private across casts and composite children: %d failures /6\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
