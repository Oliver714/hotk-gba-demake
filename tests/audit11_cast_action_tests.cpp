#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
int main() {
    int moves=0,hits=0;
    for(int side=0;side<2;++side) for(int close=0;close<2;++close) for(unsigned seed=1;seed<=32;++seed) {
        siege(1,1);game.campaign.random=seed;
        for(auto& f:game.battle.sides) {f.mp=0;f.action_ticks=60000;}
        game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
        auto& f=game.battle.sides[side];auto& enemy=game.battle.sides[1-side];
        const int direction=side?1:-1;
        f.x=f.previous_x=800;enemy.x=int16_t(800+direction*(close?40:400));enemy.hp=60;
        f.mp=999;f.charge=sg::original::cast_charge_max;f.order=sg::Order::general;
        if(close) {f.attack_ticks=7;f.attack_direction=int8_t(direction);}
        for(const auto& skill:sg::skill_defs) if(skill.script==2666)
            game.campaign.officers[f.general].learned[0]=uint8_t(skill.id);
        assert(game.cast_skill(side,0));
        for(int tick=0;tick<12;++tick) game.battle_tick();
        assert(game.battle.effect_remaining>0);
        moves+=f.x!=800;hits+=enemy.hp!=60;
    }
    std::printf("%s: active healing cast must suspend commander locomotion/melee: moved=%d, hit=%d /128\n",moves||hits?"FAIL":"PASS",moves,hits);
    return moves||hits;
}
