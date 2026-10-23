#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"
#include <cstdint>

int main() {
    for(int seed=1;seed<=100;++seed) {
        siege(1,1);
        game.campaign.random=uint32_t(seed);
        game.campaign.armies[0].money=999999;
        auto oracle=game;
        // Defended-city population and defense attrition precede army funds.
        (void)oracle.roll(200);(void)oracle.roll(200);
        const auto expected_retained=uint32_t(uint64_t(999999)*uint32_t(4000+oracle.roll(2000))/10000u);
        game.finish_battle(1);
        const auto retained=game.campaign.armies[0].money;
        if(retained<399999 || retained>599899) {
            std::printf("FAIL retreat seed%d retained%u expected 40..60 percent of 999999\n",seed,retained);
            return 1;
        }
        assert(retained==expected_retained);
        siege(1,1);
        game.campaign.random=uint32_t(seed);
        game.campaign.armies[0].money=0;
        const int city=game.battle.city;
        game.campaign.cities[city].money=999999;
        oracle=game;
        const auto expected_loot=uint32_t(uint64_t(999999)*uint32_t(3000+oracle.roll(2000))/10000u);
        game.finish_battle(0);
        const auto loot=game.campaign.cities[city].money;
        if(loot<299999 || loot>499899) {
            std::printf("FAIL siege seed%d retained%u expected 30..50 percent of 999999\n",seed,loot);
            return 1;
        }
        assert(loot==expected_loot);
    }
    std::puts("PASS: 100 seeds, maximum normal funds, siege and retreat percentages");
}
