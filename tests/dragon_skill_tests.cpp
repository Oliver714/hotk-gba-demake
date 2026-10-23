#include "model.h"
#include "original_rules.h"
#include <cassert>

int main() {
    sg::Game game;
    game.new_game(1,1);
    game.campaign.phase=sg::Phase::battle;
    game.battle.pending_winner=-1;
    game.battle.row_setup_active=0;
    game.battle.sides[0].general=0;
    game.battle.sides[0].hp=100;
    game.battle.sides[0].mp=100;
    game.battle.sides[0].charge=sg::original::cast_charge_max;
    game.battle.sides[1].general=1;
    game.battle.sides[1].hp=100;
    const int skill_ids[]={14,24,32};
    const int scripts[]={2642,2643,2644};
    for(int tier=0;tier<3;++tier) {
        game.battle={};
        game.battle.pending_winner=-1;
        game.battle.sides[0].general=0;
        game.battle.sides[0].hp=100;
        game.battle.sides[0].mp=100;
        game.battle.sides[0].charge=sg::original::cast_charge_max;
        game.battle.sides[1].general=1;
        game.battle.sides[1].hp=100;
        game.campaign.officers[0].learned[0]=uint8_t(skill_ids[tier]);
        assert(game.cast_skill(0,0));
        int count=0;
        int delays[3]={};
        for(const auto& projectile : game.battle.projectiles)
            if(projectile.active && projectile.magic==1 && projectile.source_script==scripts[tier])
                delays[count++]=projectile.remaining;
        assert(count==tier+1);
        for(int i=0;i<count;++i)
            assert(delays[i]==24);
        int lane=0;
        for(const auto& p:game.battle.projectiles) if(p.active && p.source_script==scripts[tier]) {
            assert(p.target_offset_x==game.battle.sides[1].x);
            assert(p.x==p.target_offset_x+100);
            assert(p.y==sg::battle_commander_y+(2*lane-tier)*30);
            ++lane;
        }
    }
}
