#include "model.h"
#include <cstdio>
static sg::Game game;
int main() {
    int errors=0;
    for(int scenario=0;scenario<sg::scenario_count;++scenario) {
        game.new_game(scenario,0);
        for(int g=0;g<sg::general_count;++g) {
            const auto& o=game.campaign.officers[g];
            if(o.status!=sg::Status::employed) continue;
            if(!(o.forces&(1u<<o.force)) || !(o.formations&(1u<<o.formation))) {
                std::printf("mismatch scenario%d general%d force%d mask%x formation%d mask%x\n",scenario,g,o.force,o.forces,o.formation,o.formations);
                ++errors;
            }
        }
    }
    std::printf("Initial selection mismatches: %d\n",errors);
    return errors ? 1 : 0;
}
