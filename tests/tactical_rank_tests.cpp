#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstddef>
#include "formation_layouts.h"
void veterans() {
    siege(1,1);
    auto& officer=game.campaign.officers[0];
    officer.rank=40; officer.exp=90000; officer.soldiers=100; officer.front=0; officer.formation=0;
    game.start_duel();
    for(auto& soldier:game.battle.soldiers) if(soldier.side==1) soldier.hp=0;
    game.battle.sides[1].soldiers=1; game.battle.sides[1].front=0;
    game.battle.sides[1].order=sg::Order::hold;
    game.battle.sides[0].charge=game.battle.sides[1].charge=0;
    game.battle.sides[0].mp=game.battle.sides[1].mp=0;
}
int main(int argc,char** argv) {
    for(int veteran=0;veteran<2;++veteran) for(int layout=0;layout<14;++layout) {
        veterans();
        auto& officer=game.campaign.officers[0];
        officer.rank=uint8_t(veteran ? 40 : 20);
        officer.front=uint16_t(veteran ? 0 : 100);
        officer.formation=uint8_t(layout%7);
        game.battle.layout_front[0][0]=uint8_t(layout>=7);
        game.start_duel();
        int rear_rank=0;
        for(int i=0;i<100;++i) {
            const auto& soldier=game.battle.soldiers[i];
            assert(bool(soldier.rear)==bool(veteran));
            rear_rank+=!sg::soldier_in_front_rank(soldier);
        }
        assert(rear_rank==sg::formation_weights[layout][1]);
    }
    veterans();
    int front=0,back=0;
    for(int i=0;i<100;++i) {
        assert(game.battle.soldiers[i].rear);
        if(sg::soldier_in_front_rank(game.battle.soldiers[i])) ++front; else ++back;
    }
    assert(front==70 && back==30);
    for(int i=0;i<100;++i) game.battle.soldiers[i].action_ticks=1000;
    assert(game.issue_battle_command(0,sg::BattleCommand::front_run));
    int moving=0;
    for(int i=0;i<100;++i) {
        const auto& soldier=game.battle.soldiers[i];
        assert(soldier.action_ticks==(sg::soldier_in_front_rank(soldier) ? 0 : 1000));
        moving+=soldier.action_ticks==0;
    }
    assert(moving==70);
    int initial_x[100]; for(int i=0;i<100;++i) initial_x[i]=game.battle.soldiers[i].x;
    for(int tick=0;tick<120;++tick) game.battle_tick();
    int advanced=0;
    for(int i=0;i<100;++i) {
        const auto& soldier=game.battle.soldiers[i];
        if(sg::soldier_in_front_rank(soldier)) advanced+=soldier.x<initial_x[i];
        else assert(soldier.x==initial_x[i]);
        assert(soldier.rear); // Class and its damage/capacity accounting survive.
    }
    assert(advanced>0);
    veterans();
    assert(game.issue_battle_command(0,sg::BattleCommand::disperse));
    int dispersing=0;
    for(int i=0;i<100;++i) {
        const auto& soldier=game.battle.soldiers[i];
        if(soldier.move_ticks) { assert(sg::soldier_in_front_rank(soldier)); ++dispersing; }
    }
    assert(dispersing>0);
    veterans();
    if(argc==2) {
        auto* file=std::fopen(argv[1],"wb"); assert(file);
        constexpr size_t bytes=offsetof(sg::Game,battle_report);
        assert(std::fwrite(&game,1,bytes,file)==bytes); std::fclose(file);
    }
    std::puts("PASS: 100 veterans retain class; 70 tactical front / 30 rear; front advance and disperse select by deployment, not class");
}
