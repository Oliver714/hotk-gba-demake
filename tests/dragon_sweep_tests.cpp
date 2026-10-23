// Unit-test the isolated effect update without ordinary melee/AI advancing.
#define private public
#include "model.h"
#undef private
#include "original_rules.h"
#include <cassert>
#include <cstdio>

int main() {
    static sg::Game game;
    for(int side=0;side<2;++side) for(int tier=1;tier<=3;++tier) {
        game.new_game(1,1);
        game.campaign.phase=sg::Phase::battle;
        game.battle={};
        auto& battle=game.battle;
        battle.pending_winner=-1;
        for(int s=0;s<2;++s) {
            battle.sides[s].general=s;
            battle.sides[s].hp=100;
            battle.sides[s].x=s ? 1600 : 700;
            battle.sides[s].mp=100;
            battle.sides[s].charge=sg::original::cast_charge_max;
        }
        const int enemy=1-side;
        auto& force=battle.sides[enemy];
        force.soldiers=force.front=4;
        for(int i=0;i<4;++i) {
            auto& soldier=battle.soldiers[enemy*200+i];
            soldier.hp=10; soldier.side=enemy;
            soldier.x=force.x+40;
            soldier.y=sg::battle_commander_y+(i==3 ? tier*30+20 : (i-1)*tier*20);
        }
        // Friendly soldier inside the sweep must not be touched.
        auto& friendly=battle.soldiers[side*200];
        friendly.hp=10; friendly.x=force.x; friendly.y=sg::battle_commander_y;
        const int ids[3]={14,24,32};
        game.campaign.officers[side].learned[0]=ids[tier-1];
        assert(game.cast_skill(side,0));
        for(const auto& p:battle.projectiles) if(p.active) {
            assert(p.target_offset_x==force.x+40);
            assert(p.remaining==24);
        }
        for(int tick=0;tick<130;++tick)
            for(auto& p:battle.projectiles) if(p.active)
                game.update_fast_projectile(p,nullptr,nullptr);
        assert(force.hp==90); // Exactly one fixed fire hit, not one per body.
        assert(force.soldiers==1 && force.front==1);
        assert(battle.soldiers[enemy*200+3].hp==10); // Outside vertical coverage.
        assert(friendly.hp==10);
        for(const auto& p:battle.projectiles) assert(!p.active);
    }
    std::puts("PASS dragon sweep: all tiers, both sides, area damage, no friendly fire, single general hit, lifetime");
}
