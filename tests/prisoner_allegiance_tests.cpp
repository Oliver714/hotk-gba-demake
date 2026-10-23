#include "model.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static sg::Game game;

static int prepare(int scenario,int general,int captor) {
    game.new_game(scenario,captor);
    game.campaign.player=int8_t(captor);
    int city=-1;
    for(int c=0;c<sg::city_count;++c) if(game.campaign.cities[c].king==captor) {city=c;break;}
    if(city<0) {city=0;game.campaign.cities[city].king=int8_t(captor);}
    auto& officer=game.campaign.officers[general];
    officer.status=sg::Status::prisoner;officer.king=int8_t(captor);
    officer.city=int16_t(city);officer.army=-1;officer.loyalty=0;
    sg::native_origin_king(game.campaign,general)=0xff;
    return city;
}

int main(int argc,char**) {
    if(argc>1) {
        for(int s=0;s<sg::scenario_count;++s)
            for(int g=0;g<sg::general_count;++g) {
                const int loyal=sg::general_initial[s][g].loyal_king;
                if(loyal>=0) std::printf("%d\t%d\t%s\t%s\n",s,g,sg::general_defs[g].name,sg::king_defs[s][loyal].name);
            }
        return 0;
    }
    int checked=0;
    for(int scenario=0;scenario<sg::scenario_count;++scenario)
        for(int g=0;g<sg::general_count;++g) {
            const int loyal=sg::general_initial[scenario][g].loyal_king;
            if(loyal<0) continue;
            const int ruler=sg::king_defs[scenario][loyal].general;
            if(ruler==g) continue;
            const int captor=(loyal+1)%sg::scenario_defs[scenario].king_count;
            prepare(scenario,g,captor);
            auto& leader=game.campaign.officers[ruler];
            leader.king=int8_t(loyal);leader.status=sg::Status::employed;
            const auto random=game.campaign.random,secondary=game.campaign.random_secondary;
            assert(std::strcmp(game.prisoner_persuasion_block(g),"效忠原主")==0);
            const int countdown=sg::native_status2_countdown(game.campaign,g);
            for(int attempt=0;attempt<100;++attempt) assert(!game.persuade_prisoner(g));
            assert(game.campaign.officers[g].status==sg::Status::prisoner);
            assert(game.campaign.officers[g].loyalty==0);
            assert(game.campaign.random==random && game.campaign.random_secondary==secondary);
            assert(sg::native_status2_countdown(game.campaign,g)==countdown);
            assert(std::strstr(game.notice,"忠臣不事二主"));
            // A captive ruler has not yet joined: no guaranteed follower jump.
            leader.king=int8_t(captor);leader.status=sg::Status::prisoner;
            game.campaign.officers[g].loyalty=100;
            assert(!game.persuade_prisoner(g));
            assert(!std::strstr(game.notice,"忠臣不事二主"));
            // Once the ruler is employed by the captor, the follower joins,
            // even at loyalty100, without a random roll.
            leader.status=sg::Status::employed;
            game.campaign.officers[g].loyalty=100;
            assert(game.prisoner_persuasion_block(g)==nullptr);
            const auto before=game.campaign.random;
            assert(game.persuade_prisoner(g));
            assert(game.campaign.officers[g].king==captor);
            assert(game.campaign.random==before);
            // Native original-faction shortcut takes precedence over LOYAL.
            prepare(scenario,g,captor);
            game.campaign.officers[ruler].king=int8_t(loyal);
            game.campaign.officers[ruler].status=sg::Status::employed;
            sg::native_origin_king(game.campaign,g)=uint8_t(captor);
            assert(game.persuade_prisoner(g));
            ++checked;
        }
    assert(checked>150);
    // The same lock also governs AI's annual domestic prisoner pass.
    int ai_checked=0;
    for(int g=0;g<sg::general_count && ai_checked<8;++g) {
        const int loyal=sg::general_initial[0][g].loyal_king;
        if(loyal<0 || sg::king_defs[0][loyal].general==g) continue;
        const int captor=(loyal+1)%sg::scenario_defs[0].king_count;
        prepare(0,g,captor);
        game.campaign.player=int8_t((captor+1)%sg::scenario_defs[0].king_count);
        const int ruler=sg::king_defs[0][loyal].general;
        game.campaign.officers[ruler].king=int8_t(loyal);
        game.campaign.officers[ruler].status=sg::Status::employed;
        game.campaign.phase=sg::Phase::domestic;
        game.finish_domestic();
        assert(game.campaign.officers[g].status==sg::Status::prisoner);
        ++ai_checked;
    }
    assert(ai_checked==8);
    std::printf("PASS: %d scenario LOYAL bindings, repeated zero-loyalty attempts, captive/joined ruler, origin shortcut; %d AI cases\n",checked,ai_checked);
}
