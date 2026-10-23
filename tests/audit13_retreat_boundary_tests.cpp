#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstddef>
#include <cstdlib>
int main(int argc,char** argv) {
    int premature=0;
    for(int side=argc==4?std::atoi(argv[2]):0;side<(argc==4?std::atoi(argv[2])+1:2);++side)
      for(int front=argc==4?1:0;front<2;++front) {
        siege(1,1);game.battle.general_front[side][0]=uint8_t(front);game.start_duel();
        for(auto& f:game.battle.sides) {f.hp=10000;f.mp=0;f.action_ticks=60000;}
        game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
        auto& f=game.battle.sides[side];f.order=sg::Order::retreat;
        if(argc==4) {
            f.x=int16_t(f.deployment_x+(side==0?639:-639));f.previous_x=f.x;
            game.battle.camera_manual=1;game.battle.camera_x=int16_t(sg::clamp(int(f.x)-120,0,sg::battlefield_width-240));
            FILE* file=std::fopen(argv[1],"wb");assert(file);
            const size_t bytes=std::atoi(argv[3])?41736:offsetof(sg::Game,battle_report);
            assert(std::fwrite(&game,1,bytes,file)==bytes);std::fclose(file);
            std::printf("%zu\n",offsetof(sg::Game,battle)+offsetof(sg::Battle,pending_winner));return 0;
        }
        for(int tick=0;tick<4000 && game.battle.pending_winner<0;++tick) game.battle_tick();
        assert(game.battle.pending_winner==1-side);
        const bool crossed=side==0 ? f.x>=sg::battlefield_width-1 : f.x<=0;
        if(!crossed) {++premature;std::printf("Premature escape side%d front%d x%d deployment%d\n",side,front,f.x,f.deployment_x);}
    }
    std::printf("%s: escape must cross field edge regardless of front/back deployment: %d premature exits /4\n",premature?"FAIL":"PASS",premature);
    return premature;
}
