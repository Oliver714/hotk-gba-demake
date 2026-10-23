#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include <initializer_list>
int main() {
    int accepted=0;
    for(int script: {2663,2664}) {
        siege(1,1);auto& f=game.battle.sides[0];
        for(const auto& skill:sg::skill_defs) if(skill.script==script) game.campaign.officers[f.general].learned[0]=uint8_t(skill.id);
        f.mp=999;f.charge=sg::original::cast_charge_max;
        // Enough slots for the old wrapper preflight, but not its actual
        // overlapping children (20+9+2 or80+106+1 respectively).
        const int free=script==2663?19:120;
        for(int i=0;i<sg::battle_projectile_slots-free;++i) {
            auto& p=game.battle.projectiles[i];p={};p.active=1;p.magic=33;p.source_script=2618;
        }
        const int mp=f.mp;const auto charge=f.charge;
        accepted+=game.cast_skill(0,0);
        if(f.mp!=mp || f.charge!=charge) ++accepted;
    }
    for(int script: {2663,2664}) {
        siege(1,1);auto& f=game.battle.sides[0];
        for(auto& actor:game.battle.sides) {actor.hp=10000;actor.mp=0;actor.action_ticks=60000;}
        game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
        for(const auto& skill:sg::skill_defs) if(skill.script==script) game.campaign.officers[f.general].learned[0]=uint8_t(skill.id);
        f.mp=999;f.charge=sg::original::cast_charge_max;
        const int budget=sg::original::composite_projectile_reservation(script);
        for(int i=0;i<sg::battle_projectile_slots-budget;++i) {
            auto& p=game.battle.projectiles[i];p={};p.active=1;p.magic=33;p.source_script=2618;
        }
        assert(game.cast_skill(0,0));const int spent_mp=f.mp;
        unsigned seen=0;
        for(int i=0;i<1000 && game.battle.magic_composite_kind;++i) {
            game.battle_tick();
            for(const auto& p:game.battle.projectiles) if(p.active) {
                if(p.source_script==(script==2663?2617:2604)) seen|=1;
                if(p.source_script==(script==2663?2628:2622)) seen|=2;
                if(p.source_script==(script==2663?2626:2646)) seen|=4;
            }
        }
        assert(seen==7);
        assert(!game.battle.magic_composite_kind && f.mp==spent_mp);
        for(const auto& p:game.battle.projectiles) assert(!p.active || p.magic!=37);
    }
    std::printf("%s: composite root must reject insufficient child capacity without spending MP/charge: %d failures\n",accepted?"FAIL":"PASS",accepted);
    return accepted?1:0;
}
