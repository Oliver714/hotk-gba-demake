#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include <string>
int main(int argc,char** argv) {
    int lost=0;
    int skill_id=0;for(const auto& skill:sg::skill_defs) if(skill.script==2650) skill_id=skill.id;
    assert(skill_id);
    for(int victim=argc==5?std::atoi(argv[2]):0;victim<(argc==5?std::atoi(argv[2])+1:2);++victim)
      for(int applied=argc==5?std::atoi(argv[3]):0;applied<(argc==5?std::atoi(argv[3])+1:2);++applied)
       for(int command=argc==5?5:0;command<(argc==5?6:8);++command) {
        prepare_skill_review(skill_id,1-victim,1);
        for(int tick=0;tick<70;++tick) game.battle_tick();
        auto& f=game.battle.sides[victim];auto& s=game.battle.soldiers[victim*200];
        const int delay=sg::original::magic_action_delay(2650);assert(f.action_delay==delay && s.action_delay==delay);
        if(applied) {f.action_ticks=s.action_ticks=1;game.battle_tick();assert(!f.action_delay && !s.action_delay);}
        if(argc==5) {
            game.campaign.armies[game.battle.army].king=int8_t(victim==0?game.campaign.player:0);
            game.campaign.cities[game.battle.city].king=int8_t(victim==1?game.campaign.player:0);
            for(int side=0;side<2;++side) game.campaign.officers[game.battle.sides[side].general].king=int8_t(side==victim?game.campaign.player:0);
            FILE* file=std::fopen(argv[1],"wb");assert(file);
            constexpr size_t prefix=offsetof(sg::Game,battle_report);
            // New flags are appended to Battle; baseline's prefix ends here.
            const size_t bytes=std::atoi(argv[4])?41736:prefix;
            assert(prefix>=41736 && prefix-41736<100);
            assert(std::fwrite(&game,1,bytes,file)==bytes);std::fclose(file);
            const auto* base=reinterpret_cast<const char*>(&game);
            std::printf("%td %td\n",reinterpret_cast<const char*>(&f.action_ticks)-base+1,reinterpret_cast<const char*>(&s.action_ticks)-base+1);
            return 0;
        }
        game.issue_battle_command(victim,sg::BattleCommand(command));
        lost+=applied ? f.action_ticks<delay : f.action_delay!=delay;
        lost+=applied ? s.action_ticks<delay : s.action_delay!=delay;
        game.start_duel();
        for(auto bits:game.battle.magic_delay_bits) assert(!bits);
    }
    std::printf("%s: all eight orders preserve pending/applied magic delay: %d lost timers /64; new bout clears flags\n",lost?"FAIL":"PASS",lost);
    return lost?1:0;
}
