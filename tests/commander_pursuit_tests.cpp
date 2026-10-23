#define SANGO_ROUND_NO_MAIN
#include "battle_round_tests.cpp"

void setup_chase(int pursuer) {
    siege(1,1);
    for(int side=0;side<2;++side) {
        auto& f=game.battle.sides[side];
        f.hp=1000;f.mp=0;f.charge=0;f.soldiers=f.front=0;
        f.action_ticks=60000;f.attack_ticks=0;f.move_command8=0;
        f.order=side==pursuer ? sg::Order::general : sg::Order::retreat;
        f.x=f.previous_x=int16_t(side ? 900 : 1100);
        f.deployment_x=f.x;
        game.battle.ai_decision_countdown[side]=60000;
    }
    for(auto& soldier:game.battle.soldiers) soldier={};
}
int main() {
    int retreat_steps=0;
    for(int tick=1;tick<=150;++tick) retreat_steps+=sg::original::commander_retreat_step(tick);
    assert(retreat_steps==60); // Forward: 100; retreat: 60, without rounding drift.
    for(int pursuer=0;pursuer<2;++pursuer) {
        setup_chase(pursuer);
        auto& own=game.battle.sides[pursuer];auto& enemy=game.battle.sides[1-pursuer];
        const int initial_own=own.x,initial_enemy=enemy.x;
        for(int tick=0;tick<150;++tick) game.battle_tick();
        const int forward=pursuer ? 1 : -1;
        assert((own.x-initial_own)*forward==100);
        assert((enemy.x-initial_enemy)*forward==60);
        assert((enemy.x-own.x)*forward==160); // Gap closes, never remains fixed.
        own.action_ticks=0;
        bool attacked=false;
        for(int tick=0;tick<500 && game.battle.pending_winner<0;++tick) {
            game.battle_tick();attacked|=own.attack_ticks!=0;
        }
        assert(attacked); // Pursuer can enter the actual attack reach.

        // A nearby soldier still blocks pursuit; do not run through a rank.
        setup_chase(pursuer);
        auto& target=game.battle.soldiers[(1-pursuer)*200];
        target.hp=10;target.side=uint8_t(1-pursuer);target.action_ticks=60000;
        target.x=target.previous_x=int16_t(own.x+forward*20);
        target.y=target.previous_y=sg::battle_commander_y;
        game.battle.sides[1-pursuer].soldiers=game.battle.sides[1-pursuer].front=1;
        const int held_x=own.x;
        for(int tick=0;tick<30;++tick) game.battle_tick();
        assert(own.x==held_x);

        // Allied ranks block either side's main-general order, too.
        setup_chase(pursuer);
        auto& ally=game.battle.soldiers[pursuer*200];
        ally.hp=10;ally.side=uint8_t(pursuer);ally.action_ticks=60000;
        ally.x=ally.previous_x=int16_t(own.x+forward*sg::battle_grid_column_width);
        ally.y=ally.previous_y=sg::battle_commander_y;
        own.soldiers=own.front=1;
        const int allied_held_x=own.x;
        for(int tick=0;tick<60;++tick) game.battle_tick();
        assert(own.x==allied_held_x && own.move_command8==0);
        // An adjacent row is not a blocker. Resume without reissuing orders.
        ally.y=ally.previous_y=sg::battle_commander_y+sg::battle_grid_row_height;
        for(int tick=0;tick<30;++tick) game.battle_tick();
        assert((own.x-allied_held_x)*forward>0);
        // A dead allied occupant must not block the lane either.
        ally.y=ally.previous_y=sg::battle_commander_y;
        ally.x=ally.previous_x=int16_t(own.x+forward*sg::battle_grid_column_width);
        ally.hp=0;own.soldiers=own.front=0;
        const int cleared_x=own.x;
        for(int tick=0;tick<30;++tick) game.battle_tick();
        assert((own.x-cleared_x)*forward>0);

        // A retreating commander without a pursuer still reaches the exit.
        setup_chase(pursuer);
        own.order=sg::Order::hold;
        // Keep the holding opponent behind the retreat, clear of the exit lane.
        own.x=own.previous_x=int16_t(pursuer ? 2100 : 100);
        constexpr int escape_timeout=sg::battlefield_width*3+150;
        int ticks=0;
        while(game.battle.pending_winner<0 && ticks<escape_timeout) { game.battle_tick();++ticks; }
        assert(game.battle.pending_winner==pursuer && ticks<escape_timeout);
        std::printf("PASS pursuit side%d: 100:60 movement, catch, rank block, escape %d ticks\n",pursuer,ticks);
    }
}
