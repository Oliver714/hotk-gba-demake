#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <initializer_list>
static void setup(int side,int force,bool close) {
    siege(1,1);
    for(int team=0;team<2;++team) {
        auto& f=game.battle.sides[team];f.hp=1000;f.mp=0;f.soldiers=f.front=1;f.order=sg::Order::hold;
        f.force=uint8_t(team==side?force:0);f.action_ticks=60000;f.ai_action_flags=sg::fighter_opening_finished_flag;
    }
    for(auto& s:game.battle.soldiers) s={};
    auto& shooter=game.battle.soldiers[side*200];shooter.hp=1;shooter.side=uint8_t(side);
    shooter.x=shooter.previous_x=1000;shooter.y=shooter.previous_y=60;
    auto& target=game.battle.soldiers[(1-side)*200];target.hp=1;target.side=uint8_t(1-side);
    target.x=target.previous_x=int16_t(1000+(side?1:-1)*(close?40:400));target.y=target.previous_y=60;
    target.action_ticks=60000;
}
int main() {
    int violations=0,total_attempts=0,total_starts=0;
    for(int side=0;side<2;++side) for(int force:{2,4,5}) for(bool close:{false,true}) {
        setup(side,force,close);auto& shooter=game.battle.soldiers[side*200];
        int attempts=0,starts=0,shots=0,last_shot=-10000,min_gap=10000;
        const int delay=sg::original::soldier_attack_event_delay(force,side,false);
        for(int tick=0;tick<(close?1800:9000);++tick) {
            game.battle.duel_tick=0;
            for(int team=0;team<2;++team) {
                game.battle.sides[team].action_ticks=60000;
                game.battle.ai_decision_countdown[team]=60000;
            }
            game.battle.soldiers[(1-side)*200].action_ticks=60000;
            const bool ready=shooter.action_ticks<=1 && !shooter.attack_ticks;
            game.battle_tick();
            if(ready) {
                ++attempts;starts+=shooter.attack_ticks>0;
                if(close) violations+=!shooter.attack_ticks || shooter.action_ticks!=delay+30;
                else if(shooter.attack_ticks) violations+=shooter.action_ticks<delay+45 || shooter.action_ticks>delay+60;
                else violations+=shooter.action_ticks<15 || shooter.action_ticks>30;
            }
            for(auto& p:game.battle.projectiles) if(p.active) {
                if(p.magic==0) {
                    ++shots;
                    if(last_shot>=0) min_gap=sg::minimum(min_gap,tick-last_shot);
                    last_shot=tick;
                    violations+=close?shooter.action_ticks!=30:shooter.action_ticks<45 || shooter.action_ticks>60;
                }
                p.active=0; // Count actual releases without killing the stationary target.
            }
        }
        if(close) violations+=starts!=attempts || min_gap<delay+30;
        else {total_attempts+=attempts;total_starts+=starts;violations+=min_gap<delay+45;}
        std::printf("side%d force%d %s: attempts%d starts%d arrows%d min_gap%d updates\n",side,force,close?"close":"distant",attempts,starts,shots,min_gap);
    }
    violations+=total_starts*100<total_attempts*5 || total_starts*100>total_attempts*14;
    std::printf("%s: distant trigger %d/%d (demake 9%%); randomized 15..30 checks, 30-update post-shot recovery, close attacks bypass chance; violations%d\n",violations?"FAIL":"PASS",total_starts,total_attempts,violations);
    return violations?1:0;
}
