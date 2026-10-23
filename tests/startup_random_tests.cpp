#include "model.h"
#include "original_rules.h"
#include <cassert>
#include <cstdio>

namespace {
sg::Game game;
struct Sample {
    uint32_t primary,secondary;
    uint32_t outcomes[12];
};
Sample search_after_menu_wait(int updates) {
    game={}; // A fresh process, as opposed to returning to the title.
    for(int i=0;i<updates;++i) game.advance_menu_random();
    game.new_game(1,1);
    Sample sample{game.campaign.random,game.campaign.random_secondary,{}};
    int worker=-1;
    const int ruler=sg::king_defs[1][1].general;
    for(int g=0;g<sg::general_count;++g) {
        const auto& officer=game.campaign.officers[g];
        if(g!=ruler && officer.king==1 && officer.city>=0 &&
           officer.status==sg::Status::employed &&
           (sg::general_defs[g].str>=85 || sg::general_defs[g].intel>=85)) { worker=g; break; }
    }
    assert(worker>=0);
    auto& officer=game.campaign.officers[worker];
    for(int i=0;i<12;++i) {
        const int before=officer.item_count;
        officer.worked=0; // Repeat the same search action pattern in each fixture.
        assert(game.domestic_action(officer.city,worker,2));
        const unsigned item=officer.item_count>before ? sg::inventory_item(officer,officer.item_count-1)+1 : 0;
        sample.outcomes[i]=uint16_t(game.search_result_general) |
            (uint32_t(game.search_result_joined)<<16) | (item<<24);
    }
    return sample;
}
}

int main() {
    const auto first=search_after_menu_wait(40);
    const auto repeat=search_after_menu_wait(40);
    assert(first.primary==repeat.primary && first.secondary==repeat.secondary);
    for(int i=0;i<12;++i) assert(first.outcomes[i]==repeat.outcomes[i]);
    int changed_sequences=0;
    for(int wait=100;wait<=280;wait+=60) {
        const auto other=search_after_menu_wait(wait);
        assert(first.primary!=other.primary || first.secondary!=other.secondary);
        bool changed=false;
        for(int i=0;i<12;++i) changed|=first.outcomes[i]!=other.outcomes[i];
        changed_sequences+=changed;
    }
    assert(changed_sequences>0);
    const auto cold=search_after_menu_wait(0);
    game.new_game(1,1);
    assert(game.campaign.random!=cold.primary || game.campaign.random_secondary!=cold.secondary);
    std::printf("PASS: identical menu timing replays; %d different waits change search sequences; new campaigns retain process RNG.\n",changed_sequences);
}
