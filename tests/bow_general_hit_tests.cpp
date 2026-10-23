#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"
#include <initializer_list>
#include <string>

int main(int argc,char** argv) {
    if(argc==5) {
        const int side=std::atoi(argv[3]);
        const int id=std::atoi(argv[2]);
        prepare_skill_review(id,side,0);
        auto& caster=game.battle.sides[side];
        auto& target=game.battle.sides[1-side];
        target.x=target.previous_x=int16_t(caster.x+std::atoi(argv[4]));
        FILE* file=std::fopen(argv[1],"wb"); assert(file);
        assert(std::fwrite(&game,1,offsetof(sg::Game,battle_report),file)==offsetof(sg::Game,battle_report));
        std::fclose(file);
        const uint16_t hp=uint16_t(target.hp-sg::original::magic_general_hit_bonus(sg::skill_by_id(id)->script));
        const auto expected=std::string(argv[1])+".hp";
        file=std::fopen(expected.c_str(),"wb"); assert(file);
        assert(std::fwrite(&hp,2,1,file)==1); std::fclose(file);
        std::printf("%zu\n",offsetof(sg::Game,battle)+offsetof(sg::Battle,sides)+
            sizeof(sg::Fighter)*(1-side)+offsetof(sg::Fighter,hp));
        return 0;
    }
    for(int id : {16,55}) for(int side : {0,1}) for(int offset : {-300,-10,0,10,300}) {
        prepare_skill_review(id,side,0);
        auto& caster=game.battle.sides[side];
        auto& target=game.battle.sides[1-side];
        // Simulate a general crossing the caster during the launch wind-up.
        target.x=target.previous_x=int16_t(caster.x+offset);
        const int hp=target.hp;
        for(int tick=0;tick<180;++tick) game.battle_tick();
        const int damage=hp-target.hp;
        std::printf("bow%d side%d offset%d damage%d expected%d\n",id,side,offset,damage,
            sg::original::magic_general_hit_bonus(sg::skill_by_id(id)->script));
        std::fflush(stdout);
        assert(damage==sg::original::magic_general_hit_bonus(sg::skill_by_id(id)->script));
    }
    std::puts("PASS: both bows hit once in either direction, at contact and after crossing");
}
