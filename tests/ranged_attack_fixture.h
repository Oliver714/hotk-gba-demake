#pragma once
// Targeting fixtures exercise successive eligible checks without waiting
// through the random retry timers. Cadence itself is tested separately.
static bool exercise_ranged_checks(sg::Game& state,int soldier_slot) {
    auto& soldier=state.battle.soldiers[soldier_slot];
    for(int check=0;check<128;++check) {
        soldier.action_ticks=0;
        state.battle_tick();
        if(soldier.attack_ticks) return true;
    }
    return false;
}
