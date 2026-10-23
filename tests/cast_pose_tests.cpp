#define SANGO_SKILL_REVIEW_NO_MAIN
#include "skill_review_fixture.cpp"

int main() {
    int cases=0;
    for(const auto& skill:sg::skill_defs) for(int side=0;side<2;++side) {
        prepare_skill_review(skill.id,side,100);
        assert(game.battle.cast_pose_start_tick==game.battle.tick);
        // Casting must not schedule a melee strike or change the horse facing.
        assert(game.battle.sides[side].attack_ticks==0);
        const auto start=game.battle.cast_pose_start_tick;
        const auto tick=game.battle.tick;
        game.battle.paused=1;
        for(int i=0;i<10;++i) game.battle_tick();
        assert(game.battle.tick==tick);
        assert(game.battle.cast_pose_start_tick==start);
        game.battle.paused=0;
        // A rejected attempt must not restart the pose.
        game.battle.cast_pose_start_tick=1234;
        assert(!game.cast_skill(side,0));
        assert(game.battle.cast_pose_start_tick==1234);
        ++cases;
    }
    std::printf("PASS: %d casting pose cases (both sides, pause, rejected casts, no melee)\n",cases);
}
