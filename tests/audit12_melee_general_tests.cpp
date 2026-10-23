#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstddef>
#include <cstdlib>
int main(int argc,char** argv) {
    int ignored=0;
    for(int side=argc==3?std::atoi(argv[2]):0;side<(argc==3?std::atoi(argv[2])+1:2);++side)
      for(int check=0;check<(argc==3?1:5);++check) {
        siege(1,1);
        for(auto& f:game.battle.sides) {f.mp=0;f.action_ticks=60000;f.ai_action_flags=sg::fighter_opening_finished_flag;}
        game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
        const int direction=side?1:-1;
        auto& own=game.battle.sides[side];own.force=0;own.soldiers=own.front=1;own.x=int16_t(800-direction*400);
        game.battle.sides[1-side].x=int16_t(800+direction*40);
        game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=1;
        auto& s=game.battle.soldiers[side*200];s={};s.hp=1;s.side=uint8_t(side);s.x=800;s.y=sg::battle_commander_y;s.move_target_x=s.x;s.move_target_y=s.y;
        auto& distant=game.battle.soldiers[(1-side)*200];distant={};distant.hp=1;distant.side=uint8_t(1-side);distant.x=200;distant.y=300;distant.move_target_x=distant.x;distant.move_target_y=distant.y;distant.action_ticks=60000;
        if(check==1) game.battle.sides[1-side].x=int16_t(800+direction*200);
        if(check==2) s.y=s.move_target_y=300;
        if(check==3) game.battle.sides[1-side].hp=0;
        if(check==4) {distant.hp=0;game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=0;}
        if(argc==3) {
            FILE* file=std::fopen(argv[1],"wb");assert(file);
            constexpr size_t size=offsetof(sg::Game,battle_report);
            assert(std::fwrite(&game,1,size,file)==size);std::fclose(file);
            std::printf("%zu 0\n",offsetof(sg::Game,battle)+offsetof(sg::Battle,soldiers)+side*200*sizeof(sg::Soldier)+offsetof(sg::Soldier,attack_ticks));
            return 0;
        }
        game.battle_tick();
        if(check==0) ignored+=s.attack_ticks!=0;
        else if(check==4) assert(s.attack_ticks);
        else assert(!s.attack_ticks);
    }
    std::printf("%s: remaining enemy soldiers retain priority over adjacent general: %d violations /2\n",ignored?"FAIL":"PASS",ignored);
    if(!ignored) std::puts("PASS: six controls reject distant, off-lane and dead generals\nPASS suite:audit12_melee_general");
    return ignored;
}
