#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
int main(int argc,char** argv) {
    int conflicts=0;
    for(int side=argc==3?std::atoi(argv[2]):0;side<(argc==3?std::atoi(argv[2])+1:2);++side) {
        prepare_skill_review(26,side,0);
        auto& f=game.battle.sides[side];f.soldiers=f.front=1;
        auto& moving=game.battle.soldiers[side*200];moving={};moving.hp=1;moving.side=uint8_t(side);
        const int entry=side?80:sg::battlefield_width-80;
        moving.x=int16_t(entry+(side?40:-40));moving.y=60;moving.previous_x=moving.x;moving.previous_y=60;
        moving.move_target_x=int16_t(entry);moving.move_target_y=60;moving.move_dx=int8_t(side?-1:1);moving.move_ticks=32;moving.action_ticks=60000;
        if(argc==3) {
            FILE* file=std::fopen(argv[1],"wb");assert(file);
            constexpr size_t size=offsetof(sg::Game,battle_report);
            assert(std::fwrite(&game,1,size,file)==size);std::fclose(file);
            std::printf("%zu 90\n",offsetof(sg::Game,battle)+offsetof(sg::Battle,soldiers)+(side*200+1)*sizeof(sg::Soldier)+offsetof(sg::Soldier,y));
            return 0;
        }
        for(int tick=0;tick<35;++tick) game.battle_tick();
        assert(f.soldiers==5 && moving.x==entry && moving.y==60);
        for(int i=1;i<5;++i) {
            const auto& s=game.battle.soldiers[side*200+i];
            conflicts+=s.hp>0 && s.x==entry && s.y==60;
        }
    }
    std::printf("%s: ambush must respect existing movement reservations: %d overlapping entry cells /2\n",conflicts?"FAIL":"PASS",conflicts);
    return conflicts;
}
