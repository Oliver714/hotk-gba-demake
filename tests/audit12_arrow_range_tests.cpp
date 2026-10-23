#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstddef>
#include <cstdlib>
int main(int argc,char** argv) {
    int overshot=0;
    for(int side=argc==3?std::atoi(argv[2]):0;side<(argc==3?std::atoi(argv[2])+1:2);++side) {
        siege(1,1);
        for(auto& f:game.battle.sides) {f.mp=0;f.action_ticks=60000;f.ai_action_flags=sg::fighter_opening_finished_flag;}
        game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
        auto& f=game.battle.sides[side];f.force=2;f.soldiers=f.front=1;
        const int direction=side?1:-1,origin=side?200:2040;
        auto& s=game.battle.soldiers[side*200];s={};s.hp=1;s.side=uint8_t(side);s.index=0;
        s.x=int16_t(origin);s.y=300;s.move_target_x=s.x;s.move_target_y=s.y;s.attack_direction=int8_t(direction);s.attack_ticks=1;s.action_ticks=60000;
        auto& t=game.battle.soldiers[(1-side)*200];t=s;t.side=uint8_t(1-side);t.attack_ticks=0;
        t.x=int16_t(origin+direction*400);t.move_target_x=t.x;game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=1;
        game.battle_tick();auto& arrow=game.battle.projectiles[0];assert(arrow.active && arrow.magic==0);
        t.hp=0;game.battle.sides[1-side].soldiers=game.battle.sides[1-side].front=0;
        if(argc==3) {
            FILE* file=std::fopen(argv[1],"wb");assert(file);
            constexpr size_t size=offsetof(sg::Game,battle_report);
            assert(std::fwrite(&game,1,size,file)==size);std::fclose(file);
            std::printf("%zu\n",offsetof(sg::Game,battle)+offsetof(sg::Battle,projectiles)+offsetof(sg::Projectile,active));
            return 0;
        }
        for(int tick=0;tick<270;++tick) {
            game.battle_tick();if(tick==199) assert(arrow.active);
        }
        overshot+=arrow.active!=0;
    }
    std::printf("%s: ordinary arrows must expire after1000 demake units, not cross whole field: %d overshots /2\n",overshot?"FAIL":"PASS",overshot);
    return overshot;
}
