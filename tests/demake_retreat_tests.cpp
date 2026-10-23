#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <initializer_list>
namespace sg { bool demake_battle_capture(Game&,int,int,bool); }

void opening(int hp=11,int soldiers=0,int enemy_hp=60,int enemy_soldiers=20) {
    siege(1,1);
    auto& own=game.battle.sides[1];auto& enemy=game.battle.sides[0];
    own.hp=int16_t(hp);own.soldiers=int16_t(soldiers);own.order=sg::Order::hold;
    own.attack_ticks=0;own.action_ticks=60000;own.mp=0;
    own.ai_action_flags=240u<<8;
    enemy.hp=int16_t(enemy_hp);enemy.soldiers=int16_t(enemy_soldiers);enemy.order=sg::Order::hold;
    game.battle.general_hit_ticks[1]=0;game.battle.effect_remaining=0;
}
int main() {
    opening();
    for(int i=0;i<239;++i) { sg::demake_general_opening_tick(game,1);assert(game.battle.sides[1].order==sg::Order::hold); }
    sg::demake_general_opening_tick(game,1);
    assert(game.battle.sides[1].order==sg::Order::retreat);
    for(int trial=0;trial<5;++trial) {
        opening(trial==0 ? 20 : 11,trial==1 ? 10 : 0,trial==2 ? 50 : 60,trial==2 ? 10 : 20);
        if(trial==3) game.battle.sides[0].order=sg::Order::retreat;
        if(trial==4) game.battle.sides[1].hp=0;
        for(int i=0;i<240;++i) sg::demake_general_opening_tick(game,1);
        assert(game.battle.sides[1].order!=sg::Order::retreat);
    }
    opening(20);
    for(int i=0;i<240;++i) sg::demake_general_opening_tick(game,1);
    assert(game.battle.sides[1].ai_action_flags&sg::fighter_opening_checked_flag);
    game.battle.sides[1].hp=1;
    for(int i=0;i<300;++i) sg::demake_general_opening_tick(game,1);
    assert(game.battle.sides[1].order==sg::Order::general);
    // The opening decision remains one-shot, but later combat damage can
    // enable the separate, periodic native tactical retreat decision.
    for(int i=0;i<6000 && game.battle.sides[1].order!=sg::Order::retreat;++i)
        sg::demake_general_ai_tick(game,1,false);
    assert(game.battle.sides[1].order==sg::Order::retreat);

    int retreat_trials=0;
    for(int mode=0;mode<9;++mode) for(unsigned seed=1;seed<=100;++seed) {
        opening(5,0,60,0);
        auto& own=game.battle.sides[1];auto& enemy=game.battle.sides[0];
        own.initial_hp=80;game.battle.starting_hp[1]=80;own.order=sg::Order::general;
        own.ai_action_flags=sg::fighter_opening_checked_flag|sg::fighter_opening_finished_flag;
        if(mode==1) own.hp=own.initial_hp=game.battle.starting_hp[1]=11; // No fresh damage: no re-entry loop.
        if(mode==2) { enemy.hp=5;enemy.soldiers=0; } // Both sides weak.
        if(mode==3) enemy.order=sg::Order::retreat;
        if(mode==4) own.ai_action_flags=0; // Opening has not completed.
        if(mode==5) own.soldiers=10;
        if(mode==6) own.hp=20;
        if(mode==7) own.soldiers=9;
        game.campaign.random=seed;
        game.battle.ai_decision_countdown[1]=1;
        for(int i=0;i<1200 && own.order!=sg::Order::retreat;++i)
            sg::demake_general_ai_tick(game,1,mode==8);
        const bool should_retreat=mode==0 || mode==7;
        assert((own.order==sg::Order::retreat)==should_retreat);
        retreat_trials+=should_retreat;
    }
    assert(retreat_trials==200);

    // Damage/attack/cooldown/casting do not consume idle-decision time.
    for(int mode=0;mode<4;++mode) {
        opening();auto& own=game.battle.sides[1];
        if(mode==0) own.attack_ticks=10;
        if(mode==1) game.battle.general_hit_ticks[1]=10;
        if(mode==2) own.ai_action_flags|=sg::fighter_attack_cooldown_flag;
        if(mode==3) {game.battle.effect_remaining=10;game.battle.effect_side=1;}
        sg::demake_general_opening_tick(game,1);
        assert(((own.ai_action_flags&sg::fighter_idle_timer_mask)>>8)==240);
    }
    opening();
    const int g=game.battle.sides[1].general;
    game.campaign.officers[g].hp=1;game.campaign.officers[g].mp=0;
    const uint32_t random=game.campaign.random,secondary=game.campaign.random_secondary;
    game.start_duel(false);
    assert(game.campaign.officers[g].hp==1 && game.campaign.officers[g].mp==0);
    assert(game.campaign.random==random && game.campaign.random_secondary==secondary);
    game.start_duel();
    assert(game.battle.sides[1].hp==11 && game.battle.sides[1].mp==5);
    const unsigned delay=(game.battle.sides[1].ai_action_flags&sg::fighter_idle_timer_mask)>>8;
    assert(delay>=240 && delay<=420 && delay%30==0);
    game.start_duel();assert(game.battle.sides[1].hp==21 && game.battle.sides[1].mp==10);
    game.campaign.officers[g].hp=uint16_t(game.max_hp(g));
    game.campaign.officers[g].mp=uint16_t(game.max_mp(g));
    game.start_duel();assert(game.battle.sides[1].hp==game.max_hp(g) && game.battle.sides[1].mp==game.max_mp(g));

    // Normal weapons 27..30 are NOT horses. Horse equipment is 39..42.
    for(int item : {0,27,28,29,30,39,40,41,42}) for(int escapes=0;escapes<5;++escapes) {
        opening();const int loser=game.battle.sides[1].general;
        // Remove ruler protection to isolate equipment and escape-count terms.
        for(auto& city:game.campaign.cities) if(city.king==0) city.king=-1;
        game.campaign.officers[loser].equipped_item=uint8_t(item);
        sg::set_native_escape_count(game.campaign,loser,escapes);
        for(unsigned seed=1;seed<=100;++seed) {
            auto actual=game;auto expected=game;
            actual.campaign.random=expected.campaign.random=seed;
            const bool capture=expected.roll(100)>50+(item>=39 ? 25 : 0)-escapes*25;
            assert(sg::demake_battle_capture(actual,loser,1,false)==capture);
            auto escaped=game;escaped.campaign.random=seed;
            assert(!sg::demake_battle_capture(escaped,loser,1,true));
            assert(escaped.campaign.random==seed);
        }
    }
    std::puts("PASS demake opening/recovery, 900 combat retreat/guard cases, and 4500 capture/escape cases");
}
