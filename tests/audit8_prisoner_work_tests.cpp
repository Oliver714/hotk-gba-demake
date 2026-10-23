#include "model.h"
#include <cassert>
#include <cstdio>
static sg::Game game;
int main() {
    game.new_game(1,1);
    auto& officer=game.campaign.officers[5];
    officer.status=sg::Status::prisoner;officer.king=1;officer.city=5;officer.army=-1;officer.worked=0;
    sg::native_origin_king(game.campaign,5)=1;
    assert(game.persuade_prisoner(5));
    if(!officer.worked) {
        std::puts("FAIL: newly persuaded prisoner can perform domestic work in the same year");return 1;
    }
    const auto before=game.checksum();
    assert(!game.domestic_action(5,5,2) && game.checksum()==before);
    game.campaign.phase=sg::Phase::world;game.campaign.month=12;game.campaign.month_ticks=299;
    game.world_tick();
    assert(game.campaign.phase==sg::Phase::domestic && !officer.worked);
    assert(game.domestic_action(5,5,2));
    assert(game.valid());
    std::puts("PASS: persuasion consumes this year's domestic action; next year restores it");
}
