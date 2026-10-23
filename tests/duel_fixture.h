// Diagnostic only. Links current production rules without patching them.
#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstring>
#include <cstdlib>
namespace sg { void demake_general_apply_hit(Game&,int,int); }
int officer_for(int strength,int avoid=-1) {
    int best=-1, distance=10000;
    for(int g=0;g<sg::general_count;++g) if(g!=avoid) {
        const int d=std::abs(int(sg::general_defs[g].str)-strength);
        if(d<distance) { best=g; distance=d; }
    }
    return best;
}
void prepare(int str0,int str1,int seed) {
    siege(1,1);
    const int ids[2]={officer_for(str0),str0==str1 ? officer_for(str0) : officer_for(str1,officer_for(str0))};
    const sg::Officer templates[2]={game.campaign.officers[0],game.campaign.officers[2]};
    for(int side=0;side<2;++side) {
        game.campaign.officers[ids[side]]=templates[side];
        auto& officer=game.campaign.officers[ids[side]];
        // Both treated as controlled: isolate combat maths from AI retreat,
        // tactical movement and auto-casting. Each side retains independent HP.
        officer.king=game.campaign.player;
        officer.equipped_item=0; officer.mp=0; officer.rank=1;
        std::memset(officer.learned,0,sizeof(officer.learned));
        auto& fighter=game.battle.sides[side];
        fighter.general=ids[side]; fighter.hp=120; fighter.mp=0;
        fighter.soldiers=fighter.front=0;
        fighter.x=fighter.previous_x=side ? 1080 : 1120;
        fighter.order=sg::Order::hold;
        fighter.move_command8=0; fighter.action_ticks=0; fighter.action_delay=0;
        fighter.attack_ticks=0; fighter.ai_action_flags=0;
    }
    for(auto& s:game.battle.soldiers) s.hp=0;
    game.campaign.random=uint32_t(seed+1)*2654435761u;
    game.campaign.random_secondary=uint32_t(seed+17)*2246822519u;
}
#ifndef SANGO_DUEL_AUDIT_NO_MAIN
int main() {
    const int pairs[][2]={{100,50},{100,60},{90,60},{80,60},{60,60}};
    for(const auto& pair:pairs) for(int high_side=0;high_side<2;++high_side) {
        int wins=0,losses=0,draws=0,ticks=0;
        for(int seed=0;seed<128;++seed) {
            prepare(pair[high_side ? 1 : 0],pair[high_side ? 0 : 1],seed);
            while(game.battle.pending_winner<0 && game.battle.duel_tick<6000) game.battle_tick();
            const int winner=game.battle.pending_winner;
            if(winner==high_side) ++wins; else if(winner==1-high_side) ++losses; else ++draws;
            ticks+=game.battle.duel_tick;
        }
        std::printf("DUEL stronger=%d weaker=%d high_side=%d seeds=128 wins=%d losses=%d draws=%d mean_ticks=%.1f\n",pair[0],pair[1],high_side,wins,losses,draws,ticks/128.0);
    }
    for(const auto& pair:pairs) {
        prepare(pair[0],pair[1],99);
        std::printf("PAIR %s STR%d vs %s STR%d\n",sg::general_defs[game.battle.sides[0].general].name,game.strength(game.battle.sides[0].general),sg::general_defs[game.battle.sides[1].general].name,game.strength(game.battle.sides[1].general));
        for(int side=0;side<2;++side) for(int mighty=0;mighty<2;++mighty) {
            auto& fighter=game.battle.sides[side]; auto& target=game.battle.sides[1-side];
            fighter.ai_action_flags=mighty ? sg::fighter_mighty_hit_flag : 0;
            int hits=0,damage=0;
            for(int i=0;i<10000;++i) {
                target.hp=1000;
                sg::demake_general_apply_hit(game,side,side ? 1 : -1);
                hits+=target.hp<1000; damage+=1000-target.hp;
            }
            std::printf("HIT side=%d mighty=%d samples=10000 rate=%.4f mean_damage_per_attempt=%.4f\n",side,mighty,hits/10000.0,damage/10000.0);
        }
    }
}
#endif
