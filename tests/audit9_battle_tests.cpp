#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include "ranged_attack_fixture.h"
#include <cstdlib>
#include <cstddef>
#include <initializer_list>
static bool export_fixture(int argc,char** argv) {
    if(argc!=3) return false;
    FILE* file=std::fopen(argv[2],"wb");assert(file);
    constexpr size_t bytes=offsetof(sg::Game,battle_report);
    assert(std::fwrite(&game,1,bytes,file)==bytes);std::fclose(file);
    const auto* base=reinterpret_cast<const char*>(&game);
    auto offset=[&](const auto& field) {return reinterpret_cast<const char*>(&field)-base;};
    std::printf("{\"tick\":%td,\"magic\":%td,\"source\":%td,\"phase\":%td,\"height\":%td,\"attack\":%td,\"attack_delay\":%d,\"hp\":%td,\"expected_hp\":%d,\"winner\":%td,\"spark\":%td}\n",
        offset(game.battle.tick),offset(game.battle.projectiles[0].magic),offset(game.battle.projectiles[0].source_script),
        offset(game.battle.projectiles[0].phase),offset(game.battle.projectiles[0].height),offset(game.battle.soldiers[0].attack_ticks),
        sg::original::soldier_attack_event_delay(2,0,0),offset(game.battle.sides[1].hp),int(game.battle.sides[1].hp),
        offset(game.battle.pending_winner),offset(game.battle.m014_spark_emitters[0].active));
    return true;
}
static void quiet() {
    siege(1,1);
    for(auto& f:game.battle.sides) {f.action_ticks=60000;f.mp=0;f.order=sg::Order::hold;}
    game.battle.ai_decision_countdown[0]=game.battle.ai_decision_countdown[1]=60000;
}
static bool check(bool ok,const char* message) {std::printf("%s: %s\n",ok?"PASS":"FAIL",message);return ok;}
static int run_case(int argc,char** argv) {
    assert(argc==2 || argc==3);const int mode=std::atoi(argv[1]);quiet();
    if(mode==1 || mode==2) {
        auto& f=game.battle.sides[0];f.force=2;f.soldiers=f.front=1;
        auto& s=game.battle.soldiers[0];s={};s.hp=1;s.side=0;s.index=0;
        s.x=800;s.y=sg::battle_commander_y;s.move_target_x=s.x;s.move_target_y=s.y;s.attack_direction=-1;
        game.battle.sides[0].x=1200;game.battle.sides[1].x=600;
        if(mode==1) {
            s.attack_ticks=1;s.action_ticks=100;
            auto& target=game.battle.soldiers[200];target={};target.hp=1;target.side=1;target.index=0;
            target.x=600;target.y=s.y;target.move_target_x=target.x;target.move_target_y=target.y;target.action_ticks=60000;
            game.battle.sides[1].soldiers=game.battle.sides[1].front=1;
            // A 3712 rock shard retires with these fields still present.
            auto& p=game.battle.projectiles[0];p={};p.magic=32;p.source_script=2622;p.height=40;
            if(export_fixture(argc,argv)) return 0;
            game.battle_tick();
            return !check(p.active && !p.magic && !p.source_script && !p.phase && !p.height,"ordinary arrow clears retired magic slot");
        }
        if(export_fixture(argc,argv)) return 0;
        return !check(exercise_ranged_checks(game,0),"ranged soldier can acquire lone enemy general outside melee range");
    }
    if(mode==3) {
        game.battle.magic_callback_damage=10;
        game.battle.sides[0].x=800;game.battle.sides[1].x=760;
        auto& dead=game.battle.sides[0];dead.hp=1;dead.attack_ticks=7;dead.attack_direction=-1;
        auto& p=game.battle.projectiles[0];p={};p.active=p.magic=p.phase=p.collision_enabled=1;
        p.source_script=2630;p.target_side=0;p.x=806;p.y=sg::battle_commander_y;p.dx=-1;
        const int hp=game.battle.sides[1].hp;
        if(export_fixture(argc,argv)) return 0;
        game.battle_tick();
        assert(dead.hp==0);
        return !check(game.battle.sides[1].hp==hp,"commander killed by in-flight skill cannot finish a melee hit");
    }
    if(mode==4) {
        game.battle.magic_callback_damage=10;
        for(int side=0;side<2;++side) {
            game.battle.sides[side].hp=1;
            auto& p=game.battle.projectiles[side];p={};p.active=p.magic=p.phase=p.collision_enabled=1;
            p.source_script=2630;p.target_side=int8_t(side);p.x=int16_t(game.battle.sides[side].x+6);
            p.y=sg::battle_commander_y;p.dx=-1;
        }
        if(export_fixture(argc,argv)) return 0;
        game.battle_tick();
        assert(game.battle.sides[0].hp==0 && game.battle.sides[1].hp==0);
        if(!check(game.battle.pending_winner==2,"simultaneous KO produces draw, not fixed side winner")) return 1;
        game.resolve_duel(2);
        assert(game.battle.row_setup_active && game.battle.roster_active_count[0]==1 && game.battle.roster_active_count[1]==1);
        assert(game.battle.captured_count==0);
        game.start_duel();
        assert(game.battle.sides[0].hp==10 && game.battle.sides[1].hp==10);
        return 0;
    }
    if(mode==5) {
        auto& emitter=game.battle.m014_spark_emitters[0];emitter.active=1;emitter.start_tick=game.battle.tick;
        emitter.x=game.battle.sides[0].x;emitter.y=sg::battle_commander_y;
        for(int i=0;i<20;++i) {emitter.variants[i]=uint8_t(i&1);emitter.azimuths[i]=uint8_t(i*12);emitter.elevations[i]=64;}
        if(argc==3) {game.battle.row_setup_active=1;export_fixture(argc,argv);return 0;}
        game.start_duel();
        return !check(!emitter.active,"new bout clears previous bout spark emitters");
    }
    return 2;
}
int main(int argc,char** argv) {
    if(argc>1) return run_case(argc,argv);
    for(char mode='1';mode<='5';++mode) {
        char value[2]={mode,0};char* args[]={argv[0],value};
        if(run_case(2,args)) return 1;
    }
    for(int side=0;side<2;++side) for(int force: {0,2,4,5}) for(int blocker=0;blocker<2;++blocker) {
        quiet();auto& f=game.battle.sides[side];f.force=uint8_t(force);f.soldiers=f.front=1;
        auto& s=game.battle.soldiers[side*200];s={};s.hp=1;s.side=uint8_t(side);
        const int dir=side?1:-1;s.x=800;s.y=sg::battle_commander_y;s.move_target_x=s.x;s.move_target_y=s.y;s.attack_direction=int8_t(dir);
        f.x=int16_t(800-dir*400);game.battle.sides[1-side].x=int16_t(800+dir*200);
        if(blocker) {
            auto& b=game.battle.soldiers[side*200+1];b=s;b.index=1;b.x=int16_t(800+dir*80);b.move_target_x=b.x;b.action_ticks=60000;
            ++f.soldiers;++f.front;
        }
        if(force) exercise_ranged_checks(game,side*200);
        else game.battle_tick();
        // demake's ranged scan skips friendly location flags and can see the
        // distant commander through an allied front unit.
        assert(bool(s.attack_ticks)==(force!=0));
    }
    std::puts("PASS: five combat regressions and 16 side/type/blocker controls");
    std::puts("PASS suite:audit9_battle");
}
