#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstddef>
#include <cstdlib>

void prepare_skill_review(int id,int side,int count) {
    siege(1,1);
    game.campaign.armies[game.battle.army].king=side==0 ? game.campaign.player : 0;
    for(int s=0;s<2;++s) {
        auto& f=game.battle.sides[s];
        f.hp=10000; f.mp=s==side ? 1000 : 0;
        f.charge=sg::original::cast_charge_max;
        f.x=f.previous_x=s ? 1000 : 1300;
        f.soldiers=f.front=uint16_t(count);
        f.order=sg::Order::hold; f.action_ticks=60000;
        game.campaign.officers[f.general].king=s==side ? game.campaign.player : 0;
        game.battle.ai_decision_countdown[s]=60000;
        for(int i=0;i<200;++i) {
            auto& soldier=game.battle.soldiers[s*200+i]; soldier={};
            soldier.hp=i<count ? 10 : 0;
            soldier.side=uint8_t(s); soldier.index=uint8_t(i);
            soldier.x=soldier.previous_x=int16_t(s ? 1100-(i/10)*24 : 1200+(i/10)*24);
            // Ten legal occupancy rows; the old 60+40*i layout extended
            // below the battlefield and could hide a selected skill target.
            soldier.y=soldier.previous_y=int16_t(30+(i%10)*30);
            soldier.action_ticks=60000;
        }
    }
    game.campaign.officers[game.battle.sides[side].general].learned[0]=uint8_t(id);
    const auto* skill=sg::skill_by_id(id);
    if(skill && sg::original::reinforcement_amount(skill->script)) {
        // Support skills need a legal roster with room to replenish. The
        // caller still chooses --troops 100 to exercise the full-army branch.
        auto& f=game.battle.sides[side];
        game.campaign.officers[f.general].rank=30;
        f.initial_soldiers=game.max_soldiers(f.general);
        f.front=uint16_t(sg::minimum(count,game.front_capacity(f.general)));
        for(int i=0;i<200;++i)
            game.battle.soldiers[side*200+i].rear=uint8_t(i>=f.front);
    }
    if(skill && sg::original::healing_amount(skill->script))
        game.battle.sides[side].hp=int16_t(game.max_hp(game.battle.sides[side].general)-30);
    assert(game.cast_skill(side,0));
}

#ifndef SANGO_SKILL_REVIEW_NO_MAIN
int main(int argc,char** argv) {
    if(argc==5 || argc==6 || argc==7) {
        prepare_skill_review(std::atoi(argv[2]),std::atoi(argv[3]),std::atoi(argv[4]));
        if(argc==6) {
            auto& caster=game.battle.sides[std::atoi(argv[3])];
            caster.order=sg::Order::general;caster.action_ticks=0;
        }
        if(argc==7) {
            for(int tick=0;tick<std::atoi(argv[6]);++tick) game.battle_tick();
            game.battle.paused=1; // Rendering-only benchmark, not gameplay FPS.
        }
        FILE* file=std::fopen(argv[1],"wb"); assert(file);
        constexpr size_t bytes=offsetof(sg::Game,battle_report);
        assert(std::fwrite(&game,1,bytes,file)==bytes); std::fclose(file);
        std::printf("capture_troops_word %zu\n",offsetof(sg::Game,battle)+offsetof(sg::Battle,sides)+
            size_t(std::atoi(argv[3]))*sizeof(sg::Fighter)+offsetof(sg::Fighter,mp));
        std::printf("capture_position_word %zu\n",offsetof(sg::Game,battle)+offsetof(sg::Battle,sides)+
            size_t(std::atoi(argv[3]))*sizeof(sg::Fighter)+offsetof(sg::Fighter,front));
        std::printf("capture_initial_x %d\n",int(game.battle.sides[std::atoi(argv[3])].x));
        return 0;
    }
    std::puts("id\tside\ttroops\tpeak_objects\tlast_active_tick\tremaining\tactive\tname");
    int leaks=0;
    for(const auto& skill:sg::skill_defs) for(int side=0;side<2;++side)
        for(int count=0;count<=200;count+=100) {
            prepare_skill_review(skill.id,side,count);
            int peak=0,last=0,active=0;
            for(int tick=0;tick<1500;++tick) {
                game.battle_tick(); active=0;
                for(const auto& p:game.battle.projectiles) active+=p.active!=0;
                if(active || game.battle.effect_remaining) last=tick;
                if(active>peak) peak=active;
            }
            std::printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",skill.id,side,count,peak,last,game.battle.effect_remaining,active,skill.name);
            if(argc==2 && active && side==0 && count==100)
                for(const auto& p:game.battle.projectiles) if(p.active)
                    std::fprintf(stderr,"skill%d magic%d phase%d age%d remain%d source%d\n",skill.id,p.magic,p.phase,p.height,p.remaining,p.source_script);
            // Native #9999 hold frames are intentional static scenery.
            // No unlaunched/waiting tokens may remain behind those holds.
            leaks+=game.battle.effect_remaining!=0;
            for(const auto& p:game.battle.projectiles) if(p.active) {
                const bool static_hold=(p.magic==23 && p.phase==1) ||
                    (p.magic==27 && p.phase==1) || (p.magic==33 && p.height==40);
                leaks+=!static_hold;
            }
        }
    return leaks ? 1 : 0;
}
#endif
