#include "model.h"
#include "original_rules.h"
#include <assert.h>
#include <cstddef>
#include <cstring>
#include <stdio.h>
#include <utility>

namespace sg {
int demake_soldier_type(int native_force);
int demake_soldier_block_rate(int attack_type,int target_type);
void demake_soldier_apply_hit(Game& game,int side,int direction,int source_x,int y);
bool demake_soldier_projectile_hit(Game& game,int attacker_side,int target_index);
bool demake_soldier_damage_general(Game& game,int attacker_side);
void demake_general_apply_hit(Game& game,int side,int direction);
bool demake_battle_capture(Game& game,int loser_id,int winner_king,bool escaped);
}

namespace {
static_assert(offsetof(sg::Campaign,cities)==14348 && sizeof(sg::City)==20,
              "mGBA city fixture offsets must match the campaign layout");
sg::Game game;
void stable_at(int source_line) {
    if(game.campaign.phase != sg::Phase::battle && !game.valid()) {
        printf("Invalid campaign at test line %d: scenario %d year %d phase %d tick %d\n", source_line, game.campaign.scenario, game.campaign.year, int(game.campaign.phase), game.campaign.month_ticks);
        for(int city = 0; city < sg::city_count; ++city) {
            const auto& c = game.campaign.cities[city];
            if(c.king < -1 || c.king >= sg::scenario_defs[game.campaign.scenario].king_count ||
               c.reserve > sg::original::reserve_capacity(c.population) || c.major >= sg::general_count)
                printf("Invalid city %d: king=%d reserve=%u max=%u major=%d\n", city, c.king, c.reserve,
                       sg::original::reserve_capacity(c.population), c.major);
        }
        for(int g = 0; g < sg::general_count; ++g) {
            const auto& o = game.campaign.officers[g];
            if(o.city < -1 || o.city >= sg::city_count || o.king < -1 || o.king >= sg::scenario_defs[game.campaign.scenario].king_count ||
               o.army < -1 || o.army >= sg::max_armies || int(o.status) > 3 || o.force >= sg::unit_types || o.formation >= 7 ||
               o.rank > 50 || o.soldiers > game.max_soldiers(g) || o.front > o.soldiers || o.front > game.front_capacity(g) ||
               o.soldiers-o.front > game.rear_capacity(g) || o.loyalty > 100 || o.hp > game.max_hp(g) || o.mp > game.max_mp(g) ||
               o.item_count > 17 || (o.equipped_item && o.item_count > 16) || o.equipped_item >= 55 || (o.item_flags[2] & 0xfc))
                printf("Invalid officer %d: city=%d king=%d army=%d status=%d force=%u formation=%u rank=%u soldiers=%u front=%u max=%d caps=%d/%d loyalty=%u hp=%u/%d mp=%u/%d items=%u equipped=%u flags=%u\n",
                       g, o.city, o.king, o.army, int(o.status), o.force, o.formation, o.rank, o.soldiers, o.front,
                       game.max_soldiers(g), game.front_capacity(g), game.rear_capacity(g), o.loyalty, o.hp, game.max_hp(g), o.mp,
                       game.max_mp(g), o.item_count, o.equipped_item, o.item_flags[2]);
        }
        for(int i = 0; i < sg::max_armies; ++i) {
            const auto& a = game.campaign.armies[i];
            if(a.active && (a.count < 1 || a.count > 5 || a.king < 0 || a.king >= sg::scenario_defs[game.campaign.scenario].king_count ||
               a.origin < 0 || a.origin >= sg::city_count || a.target < 0 || a.target >= sg::city_count || a.remaining > a.total || a.retreating > 2))
                printf("Invalid army %d: count=%u king=%d origin=%d target=%d remaining=%u total=%u retreat=%u goal=%d\n",
                       i, a.count, a.king, a.origin, a.target, a.remaining, a.total, a.retreating, a.retreat_goal);
        }
        fflush(stdout);
        assert(false);
    }
}
#define stable() stable_at(__LINE__)
void drain_battle() {
    int bouts=0;
    for(int ticks = 0; ticks < 50000 && game.campaign.phase == sg::Phase::battle; ++ticks) {
        if(game.battle.row_setup_active) {
            game.start_duel();++bouts;
            // Confirming a bout now correctly leaves the player on hold.
            // Drive an actual battle command instead of repeatedly casting
            // slot0 (possibly healing) through passive timeout rematches.
            const int controlled=game.campaign.armies[game.battle.army].king==game.campaign.player ? 0 : 1;
            assert(game.issue_battle_command(controlled,sg::BattleCommand::assault));
        }
        game.cast_skill(0,0);
        game.battle_tick();
    }
    if(game.campaign.phase==sg::Phase::battle) {
        fprintf(stderr,"Battle drain exceeded budget: scenario=%u player=%u city=%d bouts=%d duel_tick=%u active=%u/%u pending=%d\n",game.campaign.scenario,game.campaign.player,game.battle.city,bouts,game.battle.duel_tick,game.battle.roster_active_count[0],game.battle.roster_active_count[1],game.battle.pending_winner);
        for(int side=0;side<2;++side) {
            const auto& f=game.battle.sides[side];
            fprintf(stderr,"side%d general=%d HP=%d MP=%d soldiers=%d x=%d order=%d action=%u flags=%x\n",side,f.general,f.hp,f.mp,f.soldiers,f.x,int(f.order),f.action_ticks,f.ai_action_flags);
        }
    }
    assert(game.campaign.phase != sg::Phase::battle);
    stable();
    game.finish_prisoners();
}
int next_random(uint32_t& primary,uint32_t& secondary,int limit) {
    if(limit <= 0) return 0;
    primary += 1;
    const uint32_t previous_secondary = secondary - 1;
    const unsigned secondary_shift = primary & 31;
    secondary = (previous_secondary >> secondary_shift) +
        (previous_secondary << ((32-secondary_shift)&31));
    const unsigned primary_shift = secondary & 31;
    primary = (primary << primary_shift) +
        (primary >> ((32-primary_shift)&31));
    return int((primary + secondary) % uint32_t(limit));
}
}

int main() {
    {
        sg::Game fallen;
        fallen.new_game(0,0);
        fallen.campaign.phase=sg::Phase::world;
        int city=1;
        while(city<sg::city_count && fallen.campaign.cities[city].king!=1) ++city;
        assert(city<sg::city_count);
        fallen.campaign.cities[0].king=0;
        const int ruler=sg::king_defs[0][1].general;
        const int marcher=(ruler+1)%sg::general_count,stationed=(ruler+2)%sg::general_count;
        const int captive=(ruler+3)%sg::general_count;
        auto& leader=fallen.campaign.officers[ruler];
        leader.status=sg::Status::prisoner; leader.king=0; leader.city=0; leader.army=-1;
        auto& army=fallen.campaign.armies[0];
        army={}; army.active=army.garrisoned=1; army.king=1; army.count=1;
        army.generals[0]=int16_t(marcher); army.origin=army.target=int8_t(city);
        army.retreat_goal=-1; army.money=123;
        auto& moving=fallen.campaign.officers[marcher];
        moving.king=1; moving.status=sg::Status::employed; moving.army=0; moving.city=-1;
        auto& home=fallen.campaign.officers[stationed];
        home.king=1; home.status=sg::Status::employed; home.city=int16_t(city); home.army=-1;
        auto& prisoner=fallen.campaign.officers[captive];
        prisoner.king=1; prisoner.status=sg::Status::prisoner; prisoner.city=-1; prisoner.army=0;
        assert(fallen.valid());
        auto lost=fallen; lost.campaign.player=1;
        const uint32_t money=fallen.campaign.cities[city].money;
        fallen.world_tick();
        assert(fallen.campaign.phase==sg::Phase::world && fallen.valid());
        for(const auto& c:fallen.campaign.cities) assert(c.king!=1);
        assert(fallen.campaign.cities[city].money==money+123);
        assert(!army.active && army.money==0);
        assert(moving.king==-1 && moving.city==city && moving.army==-1 && moving.status==sg::Status::wild);
        assert(home.king==-1 && home.status==sg::Status::wild);
        assert(prisoner.king==-1 && prisoner.status==sg::Status::wild && prisoner.city==city && prisoner.army==-1);
        assert(leader.king==0 && leader.status==sg::Status::prisoner && leader.city==0);
        lost.world_tick();
        assert(lost.campaign.phase==sg::Phase::ended && lost.valid());
    }
    {
        sg::Game crowded;
        crowded.new_game(0,0);
        crowded.campaign.phase=sg::Phase::world;
        for(auto& o:crowded.campaign.officers) {
            o.status=sg::Status::absent; o.king=-1; o.city=-1; o.army=-1;
        }
        crowded.campaign.cities[0].king=0; crowded.campaign.cities[1].king=1;
        auto& player=crowded.campaign.officers[0];
        player.status=sg::Status::employed; player.king=0; player.city=0;
        for(int i=0;i<sg::max_armies;++i) {
            auto& a=crowded.campaign.armies[i]; a={};
            a.active=1; a.garrisoned=1; a.king=1; a.origin=a.target=1;
            a.retreat_goal=-1; a.count=1; a.generals[0]=int16_t(i+2); a.money=10;
            auto& o=crowded.campaign.officers[i+2];
            o.status=sg::Status::employed; o.king=1; o.army=int16_t(i);
        }
        auto& prisoner=crowded.campaign.officers[34];
        prisoner.status=sg::Status::prisoner; prisoner.king=1; prisoner.army=1;
        assert(crowded.valid());
        const int party[1]={0};
        const int invalid_party[1]={255};
        const uint32_t before=crowded.checksum();
        assert(!crowded.send_army(0,1,invalid_party,1));
        assert(crowded.checksum()==before);
        auto moving=crowded;
        for(auto& a:moving.campaign.armies) a.garrisoned=0;
        const uint32_t moving_before=moving.checksum();
        assert(!moving.send_army(0,1,party,1));
        assert(moving.checksum()==moving_before); // A genuine full moving pool still refuses safely.
        auto packed=crowded;
        for(int g=2;g<sg::general_count;++g) {
            auto& o=packed.campaign.officers[g];
            o.status=sg::Status::absent; o.king=-1; o.city=-1; o.army=-1;
        }
        int next_general=2;
        for(int i=0;i<sg::max_armies;++i) {
            auto& a=packed.campaign.armies[i];
            a.garrisoned=uint8_t(i<3); a.count=uint8_t(i<3 ? 3 : 1);
            for(int j=0;j<a.count;++j) {
                a.generals[j]=int16_t(next_general);
                auto& o=packed.campaign.officers[next_general++];
                o.status=sg::Status::employed; o.king=1; o.army=int16_t(i);
            }
        }
        auto& packed_captive=packed.campaign.officers[40];
        packed_captive.status=sg::Status::prisoner; packed_captive.king=1; packed_captive.army=2;
        assert(packed.valid());
        assert(packed.send_army(0,1,party,1,false,false,0));
        int packed_generals=0; unsigned packed_money=0;
        for(const auto& a:packed.campaign.armies) if(a.active && a.king==1) {
            packed_generals+=a.count; packed_money+=a.money;
        }
        assert(packed_generals==38 && packed_money==320 && packed_captive.army==1);
        assert(packed.valid());
        assert(crowded.send_army(0,1,party,1,false,false,0));
        unsigned money=0; int generals=0;
        for(const auto& a:crowded.campaign.armies) if(a.active && a.king==1) {
            money+=a.money; generals+=a.count;
        }
        assert(money==320 && generals==32);
        assert(prisoner.army==0 && prisoner.city==-1 && prisoner.king==1);
        for(int g=2;g<34;++g) assert(crowded.campaign.officers[g].king==1 && crowded.campaign.officers[g].army>=0);
        assert(crowded.valid());
    }
    {
        sg::Game reserves;
        reserves.new_game(0,0);
        for(int city=0;city<sg::city_count;++city) {
            auto& c=reserves.campaign.cities[city];
            const auto original=c;
            c.population=0; // Repeated defended sieges must not invalidate the save.
            c.reserve=uint16_t(sg::original::demake_reserve_capacity(city));
            assert(reserves.valid());
            ++c.reserve;
            assert(!reserves.valid());
            c=original;
        }
    }
    static_assert(sizeof(sg::Fighter)==56 && sizeof(sg::Soldier)==30 && sizeof(sg::Projectile)==24,"Emulator fixture layout changed");
    static_assert(offsetof(sg::Fighter,charge)==24,"Emulator charge fixture offset changed");
    // demake MagicConfig.xml is authoritative for the three direct general
    // callbacks below; these values must not regress to the native-PC table.
    assert(sg::original::magic_general_hit_bonus(2635)==8);
    assert(sg::original::magic_general_hit_bonus(2633)==16);
    assert(sg::original::magic_general_hit_bonus(2660)==14);
    assert(sg::original::magic_general_hit_bonus(2617)==12);
    {
        // ShenGuiLuanWu is a commander-only four-hit effect in demake.  Keep
        // one live soldier in the target cell to prove the GBA visual field
        // does not fall through to ordinary soldier collision logic.
        sg::Game magic_game;
        magic_game.new_game(0,0);
        magic_game.campaign.phase=sg::Phase::battle;
        magic_game.battle.field_battle=1;
        magic_game.battle.city=-1;
        magic_game.battle.terrain=0;
        magic_game.battle.attacker_count=magic_game.battle.defender_count=1;
        magic_game.battle.attacker_index=magic_game.battle.defender_index=0;
        magic_game.battle.attackers[0]=0;
        magic_game.battle.defenders[0]=1;
        magic_game.campaign.officers[0].learned[0]=78; // demake sequence47, script2665.
        magic_game.campaign.officers[0].mp=100;
        magic_game.campaign.officers[0].soldiers=0;
        magic_game.campaign.officers[0].front=0;
        magic_game.campaign.officers[0].hp=1000;
        magic_game.campaign.officers[1].soldiers=1;
        magic_game.campaign.officers[1].front=1;
        magic_game.campaign.officers[1].hp=1000;
        magic_game.start_duel();
        magic_game.battle.sides[0].x=900;
        magic_game.battle.sides[1].x=1000;
        magic_game.battle.sides[0].order=sg::Order::hold;
        magic_game.battle.sides[1].order=sg::Order::hold;
        magic_game.battle.sides[0].charge=sg::original::cast_charge_max;
        magic_game.battle.sides[0].mp=100;
        magic_game.battle.sides[1].hp=1000;
        auto& target_soldier=magic_game.battle.soldiers[200];
        target_soldier.hp=1;
        target_soldier.side=1;
        target_soldier.index=0;
        target_soldier.x=1000;
        target_soldier.y=0;
        magic_game.battle.sides[1].soldiers=magic_game.battle.sides[1].front=1;
        assert(magic_game.cast_skill(0,0));
        bool saw_shen_gui_visual=false;
        for(int tick=0;tick<120;++tick) {
            magic_game.battle_tick();
            for(const auto& projectile : magic_game.battle.projectiles)
                if(projectile.active && projectile.magic==13 && projectile.source_script==2665)
                    saw_shen_gui_visual=true;
        }
        assert(magic_game.battle.sides[1].hp==950);
        assert(magic_game.battle.soldiers[200].hp==1);
        assert(!magic_game.battle.magic_field_iterations);
        assert(saw_shen_gui_visual);
        // At long range demake still shows its first slash, but never calls
        // OnDamage on the opposing general.
        magic_game.start_duel();
        magic_game.battle.sides[0].x=400;
        magic_game.battle.sides[1].x=1800;
        magic_game.battle.sides[0].charge=sg::original::cast_charge_max;
        magic_game.battle.sides[0].mp=100;
        magic_game.battle.sides[1].hp=1000;
        assert(magic_game.cast_skill(0,0));
        for(int tick=0;tick<80;++tick) magic_game.battle_tick();
        assert(magic_game.battle.sides[1].hp==1000);
    }
    {
        // demake YellowDragon launches ten FireDown objects. Each one uses an
        // 80x80 fire region with type -1, so clustered soldiers die without
        // a block check; the old GBA path rendered fire but never resolved it.
        sg::Game dragon_game;
        dragon_game.new_game(0,0);
        dragon_game.campaign.phase=sg::Phase::battle;
        dragon_game.battle.field_battle=1;
        dragon_game.battle.city=-1;
        dragon_game.battle.terrain=0;
        dragon_game.battle.attacker_count=dragon_game.battle.defender_count=1;
        dragon_game.battle.attacker_index=dragon_game.battle.defender_index=0;
        dragon_game.battle.attackers[0]=0;
        dragon_game.battle.defenders[0]=1;
        dragon_game.campaign.officers[0].learned[0]=33; // demake sequence14, script2617.
        dragon_game.campaign.officers[0].mp=100;
        dragon_game.campaign.officers[0].soldiers=0;
        dragon_game.campaign.officers[0].front=0;
        dragon_game.campaign.officers[0].hp=1000;
        dragon_game.campaign.officers[1].soldiers=3;
        dragon_game.campaign.officers[1].front=3;
        dragon_game.campaign.officers[1].hp=1000;
        dragon_game.campaign.officers[1].mp=0;
        dragon_game.start_duel();
        dragon_game.battle.sides[0].order=sg::Order::hold;
        dragon_game.battle.sides[1].order=sg::Order::hold;
        dragon_game.battle.sides[1].mp=0;
        dragon_game.battle.sides[1].charge=0;
        for(auto& slot : dragon_game.battle.sides[1].ai_command_slots) slot=0;
        dragon_game.battle.sides[0].charge=sg::original::cast_charge_max;
        dragon_game.battle.sides[0].mp=100;
        dragon_game.battle.sides[1].hp=1000;
        for(int index=0;index<3;++index) {
            auto& soldier=dragon_game.battle.soldiers[200+index];
            soldier.hp=1;
            soldier.side=1;
            soldier.index=uint8_t(index);
            soldier.x=1000;
            soldier.y=0;
            soldier.move_target_x=1000;
            soldier.move_target_y=0;
            soldier.action_ticks=10000;
        }
        assert(dragon_game.cast_skill(0,0));
        for(int tick=0;tick<600;++tick) dragon_game.battle_tick();
        assert(dragon_game.battle.sides[1].soldiers==0);
        assert(dragon_game.battle.sides[1].hp==1000);
    }
    {
        // demake's 天地无用 is a wrapper, not an eighth native beam spell:
        // it instantiates 2617, then 2628 after 1.5 seconds, then 2626 after
        // another second. The wrapper pays its own 50 MP only once.
        sg::Game composite_game;
        composite_game.new_game(0,0);
        composite_game.campaign.phase=sg::Phase::battle;
        composite_game.battle.field_battle=1;
        composite_game.battle.city=-1;
        composite_game.battle.terrain=0;
        composite_game.battle.attacker_count=composite_game.battle.defender_count=1;
        composite_game.battle.attacker_index=composite_game.battle.defender_index=0;
        composite_game.battle.attackers[0]=0;
        composite_game.battle.defenders[0]=1;
        composite_game.campaign.officers[0].learned[0]=41; // script2663.
        composite_game.campaign.officers[0].mp=100;
        composite_game.campaign.officers[0].soldiers=0;
        composite_game.campaign.officers[0].front=0;
        composite_game.campaign.officers[0].hp=1000;
        composite_game.campaign.officers[1].soldiers=12;
        composite_game.campaign.officers[1].front=12;
        composite_game.campaign.officers[1].hp=1000;
        composite_game.campaign.officers[1].mp=0;
        composite_game.start_duel();
        composite_game.battle.sides[0].x=400;
        composite_game.battle.sides[1].x=1800;
        composite_game.battle.sides[0].order=sg::Order::hold;
        composite_game.battle.sides[1].order=sg::Order::hold;
        composite_game.battle.sides[0].charge=sg::original::cast_charge_max;
        composite_game.battle.sides[0].mp=100;
        composite_game.battle.sides[1].mp=0;
        composite_game.battle.sides[1].charge=0;
        for(int i=0;i<12;++i) {
            auto& soldier=composite_game.battle.soldiers[200+i];
            soldier.hp=1; soldier.side=1; soldier.index=uint8_t(i);
            soldier.x=int16_t(1500+(i%3)*40); soldier.y=int16_t((i%4)*30);
            soldier.move_target_x=soldier.x; soldier.move_target_y=soldier.y;
            soldier.action_ticks=10000;
        }
        assert(composite_game.cast_skill(0,0));
        assert(composite_game.battle.sides[0].mp==50);
        bool saw_dragon=false,saw_chariot=false,saw_thunder=false;
        for(int tick=0;tick<500;++tick) {
            composite_game.battle_tick();
            saw_dragon |= composite_game.battle.effect_script==2617;
            saw_chariot |= composite_game.battle.effect_script==2628;
            saw_thunder |= composite_game.battle.effect_script==2626;
            if(!composite_game.battle.magic_composite_kind && !composite_game.battle.effect_remaining) break;
        }
        assert(saw_dragon && saw_chariot && saw_thunder);
        assert(composite_game.battle.sides[0].mp==50);
        assert(composite_game.campaign.officers[0].mp==50);
        assert(composite_game.battle.sides[1].hp<=960);
        assert(!composite_game.battle.magic_composite_kind);
    }
    {
        // demake's 鬼哭神号 starts 火箭烈 and 炬石轰 together, then starts
        // 分身斩 one second later. Both opening child resources must exist,
        // while the 50 MP wrapper cost is not multiplied by child MP costs.
        sg::Game composite_game;
        composite_game.new_game(0,0);
        composite_game.campaign.phase=sg::Phase::battle;
        composite_game.battle.field_battle=1;
        composite_game.battle.city=-1;
        composite_game.battle.terrain=0;
        composite_game.battle.attacker_count=composite_game.battle.defender_count=1;
        composite_game.battle.attacker_index=composite_game.battle.defender_index=0;
        composite_game.battle.attackers[0]=0;
        composite_game.battle.defenders[0]=1;
        composite_game.campaign.officers[0].learned[0]=81; // script2664.
        composite_game.campaign.officers[0].mp=100;
        composite_game.campaign.officers[0].soldiers=0;
        composite_game.campaign.officers[0].front=0;
        composite_game.campaign.officers[0].hp=1000;
        composite_game.campaign.officers[1].soldiers=20;
        composite_game.campaign.officers[1].front=20;
        composite_game.campaign.officers[1].hp=1000;
        composite_game.campaign.officers[1].mp=0;
        composite_game.start_duel();
        composite_game.battle.sides[0].x=400;
        composite_game.battle.sides[1].x=1800;
        composite_game.battle.sides[0].order=sg::Order::hold;
        composite_game.battle.sides[1].order=sg::Order::hold;
        composite_game.battle.sides[0].charge=sg::original::cast_charge_max;
        composite_game.battle.sides[0].mp=100;
        composite_game.battle.sides[1].mp=0;
        composite_game.battle.sides[1].charge=0;
        for(int i=0;i<20;++i) {
            auto& soldier=composite_game.battle.soldiers[200+i];
            soldier.hp=1; soldier.side=1; soldier.index=uint8_t(i);
            soldier.x=int16_t(1500+(i%4)*40); soldier.y=int16_t((i%5)*30);
            soldier.move_target_x=soldier.x; soldier.move_target_y=soldier.y;
            soldier.action_ticks=10000;
        }
        assert(composite_game.cast_skill(0,0));
        assert(composite_game.battle.sides[0].mp==50);
        bool saw_arrow=false,saw_rock=false,saw_clone=false;
        for(int tick=0;tick<500;++tick) {
            composite_game.battle_tick();
            for(const auto& projectile:composite_game.battle.projectiles) {
                saw_arrow |= projectile.active && projectile.source_script==2604;
                saw_rock |= projectile.active && projectile.source_script==2622;
            }
            saw_clone |= composite_game.battle.effect_script==2646;
            if(!composite_game.battle.magic_composite_kind && !composite_game.battle.effect_remaining) break;
        }
        assert(saw_arrow && saw_rock && saw_clone);
        assert(composite_game.battle.sides[0].mp==50);
        assert(composite_game.campaign.officers[0].mp==50);
        assert(!composite_game.battle.magic_composite_kind);
    }
    sg::Game initial_domestic;
    initial_domestic.new_game(1,1);
    const uint32_t initial_random=initial_domestic.campaign.random;
    const uint32_t initial_random_secondary=initial_domestic.campaign.random_secondary;
    initial_domestic.finish_domestic(false);
    assert(initial_domestic.campaign.phase==sg::Phase::world);
    assert(initial_domestic.campaign.random==initial_random &&
           initial_domestic.campaign.random_secondary==initial_random_secondary);
    assert(initial_domestic.valid());
    // demake leaves "Garrison" enabled for every player army. It can stop a
    // retreating army too; the GBA approximation anchors it at the nearer
    // city endpoint and clears its retreat route.
    sg::Game garrison_interaction;
    garrison_interaction.new_game(0,0);
    garrison_interaction.campaign.phase=sg::Phase::world;
    int garrison_origin=-1,garrison_target=-1,garrison_general=-1;
    for(int source=0;source<sg::city_count && garrison_origin<0;++source) {
        if(garrison_interaction.campaign.cities[source].king!=garrison_interaction.campaign.player) continue;
        int roster[sg::general_count];
        const int count=garrison_interaction.officers_at(source,garrison_interaction.campaign.player,roster,sg::general_count);
        for(int n=0;n<sg::city_neighbor_counts[source];++n) {
            const int target=sg::city_neighbor_order[source][n];
            if(garrison_interaction.campaign.cities[target].king!=garrison_interaction.campaign.player) continue;
            for(int i=0;i<count;++i) {
                const int candidate=roster[i];
                if(candidate==sg::king_defs[0][0].general) continue;
                garrison_origin=source; garrison_target=target; garrison_general=candidate; break;
            }
            if(garrison_origin>=0) break;
        }
    }
    assert(garrison_origin>=0 && garrison_general>=0);
    assert(garrison_interaction.send_army(garrison_origin,garrison_target,&garrison_general,1));
    auto& garrison_army=garrison_interaction.campaign.armies[0];
    garrison_army.retreating=2; garrison_army.retreat_goal=int8_t(garrison_origin);
    assert(garrison_interaction.garrison_army(0));
    assert(garrison_army.active && garrison_army.garrisoned && !garrison_army.retreating);
    assert(garrison_army.remaining==garrison_army.total &&
           garrison_army.origin==garrison_origin && garrison_army.target==garrison_target);
    assert(garrison_interaction.valid());
    assert(garrison_interaction.garrison_army(0)); // demake's enabled command is idempotent when already stationed.
    sg::Game army_route_interaction;
    army_route_interaction.new_game(0,0);
    army_route_interaction.campaign.phase=sg::Phase::world;
    // demake's target picker remains on the map instead of showing an empty
    // list. Confirm the imported city network connects every selectable pair,
    // so the GBA's defensive empty-target page cannot occur on a valid map.
    for(int from=0;from<sg::city_count;++from)
        for(int target=0;target<sg::city_count;++target)
            if(from!=target) assert(army_route_interaction.route_reachable(from,target));
    const uint64_t all_cities=(uint64_t(1)<<sg::city_count)-1;
    for(int from=0;from<sg::city_count;++from)
        assert(army_route_interaction.reachable_city_mask(from)==all_cities);
    assert(!army_route_interaction.reachable_city_mask(-1));
    assert(!army_route_interaction.reachable_city_mask(sg::city_count));
    assert(!army_route_interaction.route_reachable(0,-1));
    assert(!army_route_interaction.route_reachable(0,sg::city_count));
    int route_origin=-1,route_target=-1,route_general=-1;
    for(int source=0;source<sg::city_count && route_origin<0;++source) {
        if(army_route_interaction.campaign.cities[source].king!=army_route_interaction.campaign.player) continue;
        int roster[sg::general_count];
        const int count=army_route_interaction.officers_at(source,army_route_interaction.campaign.player,roster,sg::general_count);
        for(int n=0;n<sg::city_neighbor_counts[source];++n) {
            const int target=sg::city_neighbor_order[source][n];
            if(army_route_interaction.campaign.cities[target].king!=army_route_interaction.campaign.player) continue;
            for(int i=0;i<count;++i) {
                const int candidate=roster[i];
                if(candidate==sg::king_defs[0][0].general) continue;
                route_origin=source; route_target=target; route_general=candidate; break;
            }
            if(route_origin>=0) break;
        }
    }
    assert(route_origin>=0 && route_general>=0);
    assert(army_route_interaction.send_army(route_origin,route_target,&route_general,1));
    sg::Game immediate_return=army_route_interaction;
    assert(immediate_return.redirect_army(0,route_origin));
    immediate_return.world_tick();
    assert(!immediate_return.campaign.armies[0].active &&
           immediate_return.campaign.officers[route_general].city==route_origin && immediate_return.valid());
    auto& route_army=army_route_interaction.campaign.armies[0];
    const int original_total=route_army.total;
    route_army.remaining=uint16_t(original_total-1);
    route_army.retreating=2; route_army.retreat_goal=int8_t(route_origin);
    assert(army_route_interaction.redirect_army(0,route_origin));
    assert(route_army.origin==route_target && route_army.target==route_origin);
    assert(route_army.total==original_total && route_army.remaining==1);
    assert(!route_army.retreating && route_army.retreat_goal==-1 && !route_army.garrisoned);
    assert(army_route_interaction.valid());
    assert(army_route_interaction.redirect_army(0,route_target)); // clicking the old destination reverses back, as on demake's map.
    assert(route_army.origin==route_origin && route_army.target==route_target);
    assert(route_army.remaining==original_total-1);
    const int same_goal_remaining=route_army.remaining;
    route_army.retreating=1; route_army.retreat_goal=int8_t(route_target);
    assert(army_route_interaction.redirect_army(0,route_target));
    assert(route_army.remaining==same_goal_remaining && route_army.total==original_total &&
           !route_army.retreating && route_army.retreat_goal==-1);
    sg::Game pending_expedition;
    pending_expedition.new_game(0,0);
    pending_expedition.campaign.phase=sg::Phase::world;
    int pending_origin=-1,pending_general=-1,pending_remote=-1;
    for(int source=0;source<sg::city_count && pending_origin<0;++source) {
        if(pending_expedition.campaign.cities[source].king!=pending_expedition.campaign.player) continue;
        int roster[sg::general_count];
        const int count=pending_expedition.officers_at(source,pending_expedition.campaign.player,roster,sg::general_count);
        for(int i=0;i<count;++i) if(roster[i]!=sg::king_defs[0][0].general) {
            for(int target=0;target<sg::city_count;++target) {
                if(target==source || !pending_expedition.route_reachable(source,target)) continue;
                bool adjacent=false;
                for(int n=0;n<sg::city_neighbor_counts[source];++n)
                    adjacent |= sg::city_neighbor_order[source][n]==target;
                if(!adjacent) { pending_origin=source; pending_general=roster[i]; pending_remote=target; break; }
            }
            if(pending_origin>=0) break;
        }
    }
    assert(pending_origin>=0 && pending_general>=0 && pending_remote>=0);
    assert(pending_expedition.send_army(pending_origin,pending_origin,&pending_general,1,false,false,0));
    auto& pending_army=pending_expedition.campaign.armies[0];
    assert(pending_army.active && pending_army.garrisoned && pending_army.origin==pending_origin &&
           pending_army.target==pending_origin && !pending_army.total && pending_expedition.valid());
    sg::Game returned_expedition=pending_expedition;
    assert(returned_expedition.redirect_army(0,pending_origin));
    assert(!returned_expedition.campaign.armies[0].garrisoned);
    returned_expedition.world_tick();
    assert(!returned_expedition.campaign.armies[0].active &&
           returned_expedition.campaign.officers[pending_general].city==pending_origin &&
           returned_expedition.campaign.officers[pending_general].army==-1 && returned_expedition.valid());
    assert(pending_expedition.redirect_army(0,pending_remote));
    assert(!pending_army.garrisoned && pending_army.origin==pending_origin && pending_army.target==pending_remote &&
           pending_army.total && pending_army.remaining==pending_army.total && pending_expedition.valid());
    // GBA FORCE IDs are not demake armsName indices. Audit the full permutation
    // and every demake soldier block-rate pair against SolidersController.
    constexpr int native_to_demake[11]={0,1,3,2,5,4,10,7,9,6,8};
    for(int native=0;native<11;++native)
        assert(sg::demake_soldier_type(native)==native_to_demake[native]);
    auto expected_soldier_block=[](int attack,int target) {
        if(attack==0) return target==3 || target==4 || target==5 ? 20 :
            target==1 || target==2 || target==8 ? 70 : 50;
        if(attack==1) return target==0 || target==6 || target==10 ? 30 :
            target==3 || target==4 || target==5 ? 70 : 50;
        if(attack==2) return target==0 || target==7 || target==10 ? 30 :
            target==3 || target==4 || target==5 ? 70 : 50;
        if(attack>=3 && attack<=5) return target==1 || target==2 || target==6 || target==8 || target==10 ? 30 :
            target==0 || target==7 || target==9 ? 80 : 50;
        if(attack==6) return target==2 || target==8 || target==10 ? 30 :
            target==1 || target==3 || target==9 ? 70 : 50;
        if(attack==7) return target==3 || target==4 || target==5 ? 25 :
            target==1 || target==2 || target==8 ? 65 : 50;
        if(attack==8) return target==0 || target==7 || target==10 ? 25 :
            target==3 || target==4 || target==5 ? 65 : 50;
        if(attack==9) return target==3 || target==4 || target==5 ? 25 :
            target==1 || target==2 || target==8 ? 65 : 50;
        return attack==10 ? 60 : 50;
    };
    for(int attack=0;attack<11;++attack) for(int target=0;target<11;++target)
        assert(sg::demake_soldier_block_rate(attack,target)==expected_soldier_block(attack,target));
    // One demake melee event must test all occupants in its forward 4-step
    // lane, while excluding the opposite direction, adjacent rows, and units
    // beyond range. Predict the exact RNG outcomes on an identical game copy.
    sg::Game melee_game;
    melee_game.new_game(0,0);
    for(int g=0;g<sg::general_count;++g)
        assert(melee_game.campaign.officers[g].learned[0]==sg::demake_initial_magic_skills[g]);
    for(int sequence=0;sequence<50;++sequence)
        assert(sg::skill_by_id(sg::demake_magic_skill_ids[sequence])!=nullptr);
    for(auto& soldier:melee_game.battle.soldiers) soldier.hp=0;
    auto& melee_attacker=melee_game.battle.sides[0];
    auto& melee_defender=melee_game.battle.sides[1];
    melee_attacker.force=0; melee_defender.force=3;
    melee_defender.soldiers=4; melee_defender.front=2;
    const int target_x[4]={1000,1040,1080,1040};
    const int target_y[4]={180,180,180,210};
    for(int i=0;i<4;++i) {
        auto& soldier=melee_game.battle.soldiers[200+i];
        soldier.hp=1; soldier.side=1; soldier.index=uint8_t(i);
        soldier.x=int16_t(target_x[i]); soldier.y=int16_t(target_y[i]);
        soldier.rear=uint8_t(i==1 || i==3);
    }
    auto expected_melee=melee_game;
    for(int i=0;i<4;++i) {
        const int quantized_source=1000*2200/(sg::battlefield_width*12);
        const int quantized_target=target_x[i]*2200/(sg::battlefield_width*12);
        const bool in_forward_range=quantized_target>=quantized_source && quantized_target<=quantized_source+4;
        if(target_y[i]!=180 || !in_forward_range) continue;
        if(expected_melee.roll(100)>=sg::demake_soldier_block_rate(0,2)) {
            auto& soldier=expected_melee.battle.soldiers[200+i];
            soldier.hp=0;
            --expected_melee.battle.sides[1].soldiers;
            if(!soldier.rear) --expected_melee.battle.sides[1].front;
        }
    }
    sg::demake_soldier_apply_hit(melee_game,0,1,1000,180);
    assert(melee_game.battle.sides[1].soldiers==expected_melee.battle.sides[1].soldiers);
    assert(melee_game.battle.sides[1].front==expected_melee.battle.sides[1].front);
    for(int i=0;i<4;++i)
        assert(melee_game.battle.soldiers[200+i].hp==expected_melee.battle.soldiers[200+i].hp);
    // Ranged hits use the same demake troop matrix, including native FORCE 9
    // martial arts -> demake 6 and native FORCE 10 iron hammer -> demake 8.
    sg::Game projectile_game;
    projectile_game.new_game(0,0);
    for(auto& soldier:projectile_game.battle.soldiers) soldier.hp=0;
    projectile_game.battle.sides[0].force=9;
    projectile_game.battle.sides[1].force=10;
    projectile_game.battle.sides[1].soldiers=2;
    projectile_game.battle.sides[1].front=1;
    for(int i=0;i<2;++i) {
        auto& soldier=projectile_game.battle.soldiers[200+i];
        soldier.hp=1; soldier.side=1; soldier.index=uint8_t(i); soldier.rear=uint8_t(i==1);
    }
    auto expected_projectile=projectile_game;
    const int projectile_block=sg::demake_soldier_block_rate(6,8);
    bool expected_projectile_hit[2]{};
    for(int i=0;i<2;++i) {
        expected_projectile_hit[i]=expected_projectile.roll(100)>=projectile_block;
        if(expected_projectile_hit[i]) {
            expected_projectile.battle.soldiers[200+i].hp=0;
            --expected_projectile.battle.sides[1].soldiers;
            if(i==0) --expected_projectile.battle.sides[1].front;
        }
    }
    for(int i=0;i<2;++i)
        assert(sg::demake_soldier_projectile_hit(projectile_game,0,200+i)==expected_projectile_hit[i]);
    assert(projectile_game.battle.sides[1].soldiers==expected_projectile.battle.sides[1].soldiers);
    assert(projectile_game.battle.sides[1].front==expected_projectile.battle.sides[1].front);
    sg::Game general_hit_game;
    general_hit_game.new_game(0,0);
    general_hit_game.battle.sides[1].hp=100;
    auto expected_general_hit=general_hit_game;
    const int defender_strength=general_hit_game.strength(general_hit_game.battle.sides[1].general);
    const bool expected_general_damage=expected_general_hit.roll(100)<=70-defender_strength/5;
    if(expected_general_damage) expected_general_hit.battle.sides[1].hp-=2;
    assert(sg::demake_soldier_damage_general(general_hit_game,0)==expected_general_damage);
    assert(general_hit_game.battle.sides[1].hp==expected_general_hit.battle.sides[1].hp);
    // demake capture odds depend on horse equipment, king status, and the
    // cumulative escape count. Retreating skips the capture RNG roll.
    sg::Game capture_game;
    capture_game.new_game(0,0);
    const int capture_loser=1;
    auto& loser_info=capture_game.campaign.officers[capture_loser];
    loser_info.king=0; loser_info.status=sg::Status::employed; loser_info.equipped_item=39;
    capture_game.campaign.officers[2].king=1;
    sg::set_native_escape_count(capture_game.campaign,capture_loser,0);
    auto expected_capture=capture_game;
    const bool expected_capture_result=expected_capture.roll(100)>75;
    if(expected_capture_result) {
        auto& expected_loser=expected_capture.campaign.officers[capture_loser];
        expected_loser.status=sg::Status::prisoner; expected_loser.king=1;
        expected_loser.army=-1; expected_loser.city=expected_capture.battle.city;
        expected_loser.loyalty=30;
        sg::native_status2_countdown(expected_capture.campaign,capture_loser)=sg::general_defs[capture_loser].constancy;
        sg::set_native_escape_count(expected_capture.campaign,capture_loser,0);
    } else sg::set_native_escape_count(expected_capture.campaign,capture_loser,
                                       sg::native_escape_count(expected_capture.campaign,capture_loser)+1);
    assert(sg::demake_battle_capture(capture_game,capture_loser,1,false)==expected_capture_result);
    assert(capture_game.campaign.officers[capture_loser].status==expected_capture.campaign.officers[capture_loser].status);
    assert(sg::native_escape_count(capture_game.campaign,capture_loser)==
           sg::native_escape_count(expected_capture.campaign,capture_loser));
    const uint32_t escape_rng=capture_game.campaign.random;
    sg::set_native_escape_count(capture_game.campaign,capture_loser,3);
    assert(!sg::demake_battle_capture(capture_game,capture_loser,1,true));
    assert(sg::native_escape_count(capture_game.campaign,capture_loser)==4);
    assert(capture_game.campaign.random==escape_rng);
    sg::Game king_capture_game;
    king_capture_game.new_game(0,0);
    const int capture_ruler=sg::king_defs[0][0].general;
    king_capture_game.campaign.officers[capture_ruler].king=0;
    for(int city=0;city<sg::city_count;++city) if(sg::city_initial[0][city].king==0)
        king_capture_game.campaign.cities[city].king=0;
    assert(!sg::demake_battle_capture(king_capture_game,capture_ruler,1,false));
    assert(sg::native_escape_count(king_capture_game.campaign,capture_ruler)==1);
    auto seed_capture_duel=[](sg::Game& duel) {
        duel.new_game(0,0);
        const int winner=sg::king_defs[0][0].general;
        auto is_ruler=[&](int general) {
            for(int king=0;king<sg::scenario_defs[0].king_count;++king)
                if(sg::king_defs[0][king].general==general) return true;
            return false;
        };
        int loser=-1,backup0=-1,backup1=-1;
        for(int g=0;g<sg::general_count;++g) {
            if(g!=winner && !is_ruler(g) && loser<0) loser=g;
            else if(g!=winner && g!=loser && backup0<0) backup0=g;
            else if(g!=winner && g!=loser && g!=backup0 && backup1<0) backup1=g;
        }
        assert(winner>=0 && loser>=0 && backup0>=0 && backup1>=0);
        const int participants[4]={winner,loser,backup0,backup1};
        for(int g : participants) {
            auto& officer=duel.campaign.officers[g];
            officer.status=sg::Status::employed;
            officer.king=(g==loser || g==backup1) ? 1 : 0;
            officer.army=-1; officer.city=0;
        }
        duel.campaign.cities[0].king=0;
        duel.campaign.cities[1].king=1;
        duel.campaign.phase=sg::Phase::battle;
        auto& battle=duel.battle;
        battle.army=0; battle.city=1; battle.attacker_count=battle.defender_count=2;
        battle.attackers[0]=int16_t(winner); battle.attackers[1]=int16_t(backup0);
        battle.defenders[0]=int16_t(loser); battle.defenders[1]=int16_t(backup1);
        battle.attacker_index=battle.defender_index=0;
        battle.roster_active_count[0]=battle.roster_active_count[1]=2;
        for(int side=0;side<2;++side) for(int i=0;i<2;++i) battle.roster_active[side][i]=1;
        duel.start_duel();
        return loser;
    };
    sg::Game capture_duel;
    const int duel_loser=seed_capture_duel(capture_duel);
    bool found_capture_seed=false;
    for(uint32_t seed=1;seed<10000 && !found_capture_seed;++seed) {
        sg::Game probe=capture_duel;
        probe.campaign.random=seed; probe.campaign.random_secondary=0x87654321;
        found_capture_seed=probe.roll(100)>50;
        if(found_capture_seed) {
            capture_duel.campaign.random=seed;
            capture_duel.campaign.random_secondary=0x87654321;
        }
    }
    assert(found_capture_seed);
    capture_duel.battle.sides[0].hp=90;
    capture_duel.battle.sides[1].hp=0;
    capture_duel.battle.sides[1].order=sg::Order::hold;
    auto stopped_retreat=capture_duel;
    stopped_retreat.battle.sides[1].order=sg::Order::retreat;
    stopped_retreat.battle.sides[1].ai_action_flags|=sg::fighter_escaped_flag;
    stopped_retreat.resolve_duel(0);
    assert(stopped_retreat.campaign.officers[duel_loser].status==sg::Status::prisoner);
    capture_duel.resolve_duel(0);
    assert(capture_duel.campaign.officers[duel_loser].status==sg::Status::prisoner);
    assert(capture_duel.campaign.officers[duel_loser].king==0);
    assert(sg::native_escape_count(capture_duel.campaign,duel_loser)==0);
    assert(capture_duel.battle.captured_count==1);
    sg::Game escape_duel;
    const int escape_loser=seed_capture_duel(escape_duel);
    escape_duel.battle.sides[0].hp=90;
    escape_duel.battle.sides[1].hp=90;
    escape_duel.battle.sides[1].order=sg::Order::retreat;
    escape_duel.battle.sides[1].ai_action_flags|=sg::fighter_escaped_flag;
    escape_duel.resolve_duel(0);
    assert(escape_duel.campaign.officers[escape_loser].status==sg::Status::employed);
    assert(sg::native_escape_count(escape_duel.campaign,escape_loser)==1);
    sg::Campaign escape_storage{};
    for(int g=0;g<sg::general_count;++g) {
        sg::native_origin_king(escape_storage,g)=uint8_t(g%sg::max_kings);
        sg::native_status2_countdown(escape_storage,g)=uint8_t(g);
        sg::set_native_escape_count(escape_storage,g,g%8);
    }
    for(int g=0;g<sg::general_count;++g) {
        assert(sg::native_origin_king(escape_storage,g)==g%sg::max_kings);
        assert(sg::native_status2_countdown(escape_storage,g)==uint8_t(g));
        assert(sg::native_escape_count(escape_storage,g)==g%8);
    }
    // Native 0x428CC1 uses asymmetric attacker-row/defender-column data.
    assert(sg::original::force_hit_modifier(0,1) == -33);
    assert(sg::original::force_hit_modifier(1,0) == 33);
    assert(sg::original::soldier_hit_threshold(0,2) == 100);
    assert(sg::original::soldier_hit_threshold(2,0) == 200);
    assert(sg::original::force_context_modifier(0,3) == -3);
    assert(sg::original::force_context_modifier(7,8) == -20);
    assert(sg::original::soldier_hit_threshold(0,2,3) == 97);
    assert(sg::original::soldier_hit_threshold(0,2,3,550) == 103);
    assert(sg::original::soldier_followup_threshold(150) == 100);
    assert(!sg::original::soldier_hit_rolls(150,false,150,0));
    assert(sg::original::soldier_hit_rolls(150,true,150,99));
    assert(!sg::original::soldier_hit_rolls(150,true,150,100));
    assert(sg::original::soldier_hit_rolls(150,false,149,299));
    assert(sg::original::soldier_general_damage(90,false) == 1);
    assert(sg::original::soldier_general_damage(90,true) == 2);
    assert(sg::original::terrain_at(1120,80) == 2);
    assert(sg::original::terrain_at(600,409) == 1);
    assert(sg::original::terrain_at(-2,-2) == 5);
    assert(sg::original::city_annual_income(0) == 2250);
    assert(sg::original::city_annual_income(1000000) == 9750);
    assert(sg::original::demake_city_annual_income(0)==2250);
    assert(sg::original::demake_city_annual_income(200000)==3749);
    int tested = 0, battles = 0;
    for(int scenario = 0; scenario < sg::scenario_count; ++scenario) {
        for(int king = 0; king < sg::scenario_defs[scenario].king_count; ++king) {
            if(!sg::king_defs[scenario][king].selectable) continue;
            game.new_game(scenario,king);
            stable();
            ++tested;
            int city = -1, generals[255], neighbors[48], worker = -1, worker_action = -1;
            int ruler = sg::king_defs[scenario][king].general;
            for(int i = 0; i < 48 && worker < 0; ++i) if(game.campaign.cities[i].king == king) {
                int stationed[255], stationed_count = game.officers_at(i,king,stationed,255);
                for(int j = 0; j < stationed_count; ++j) {
                    int g = stationed[j];
                    if(g != ruler && sg::general_defs[g].intel >= 80) { city = i; worker = g; worker_action = 0; break; }
                    if(g != ruler && worker < 0 && sg::general_defs[g].str >= 80) { city = i; worker = g; worker_action = 1; }
                }
            }
            if(city < 0 || worker < 0) continue;
            game.officers_at(city,king,generals,255);
            int count = game.officers_at(city,king,generals,255);
            auto population = game.campaign.cities[city].population;
            auto defense = game.campaign.cities[city].defense;
            assert(game.domestic_action(city,worker,worker_action));
            if(worker_action == 0) assert(game.campaign.cities[city].population > population);
            else assert(game.campaign.cities[city].defense > defense);
            assert(!game.domestic_action(city,worker,worker_action));
            uint32_t before = game.campaign.cities[city].money;
            assert(!game.domestic_action(city,254,-1));
            assert(game.campaign.cities[city].money == before);
            game.campaign.phase = sg::Phase::world;
            int dest_count = game.neighbors(city,neighbors);
            assert(dest_count > 0);
            int invalid[2] = {generals[0],generals[0]};
            assert(!game.send_army(city,neighbors[0],invalid,2));
            assert(std::strcmp(game.notice,"出征名单不能重复选择武将") == 0);
            assert(game.campaign.officers[generals[0]].army == -1);
            assert(game.send_army(city,neighbors[0],generals,sg::minimum(count,5)));
            stable();
            for(int t = 0; t < 1200; ++t) {
                if(game.campaign.phase == sg::Phase::ended) break;
                if(game.campaign.phase == sg::Phase::domestic) game.campaign.phase = sg::Phase::world;
                if(game.campaign.phase == sg::Phase::battle) { ++battles; drain_battle(); }
                game.finish_prisoners();
                game.world_tick();
                stable();
            }
        }
    }
    {
        sg::Game capped;
        capped.new_game(1,1);
        int capped_city=-1,capped_worker=-1,stationed[255];
        const int ruler=sg::king_defs[1][1].general;
        for(int c=0;c<sg::city_count && capped_worker<0;++c) {
            if(capped.campaign.cities[c].king!=1) continue;
            const int count=capped.officers_at(c,1,stationed,255);
            for(int i=0;i<count;++i) {
                const int general=stationed[i];
                if(general!=ruler && sg::general_defs[general].intel>=80) {
                    capped_city=c; capped_worker=general; break;
                }
            }
        }
        assert(capped_city>=0 && capped_worker>=0);
        capped.campaign.cities[capped_city].population=sg::city_population_cap;
        assert(capped.domestic_action(capped_city,capped_worker,0));
        assert(capped.campaign.cities[capped_city].population==sg::city_population_cap);
        assert(capped.campaign.officers[capped_worker].worked);
        capped.campaign.officers[capped_worker].worked=0;
        capped.campaign.cities[capped_city].defense=sg::city_defense_cap;
        assert(capped.domestic_action(capped_city,capped_worker,1));
        assert(capped.campaign.cities[capped_city].defense==sg::city_defense_cap);
    }
    // Capture, retry/release, end-game and new-year paths under a tiny controlled campaign.
    game.new_game(1,1);
    for(int i = 0; i < 48; ++i) game.campaign.cities[i].king = 1;
    for(auto& o : game.campaign.officers) { o.status = sg::Status::absent; o.king = -1; o.city = -1; }
    int start = 5, dest[48];
    game.neighbors(start,dest);
    game.campaign.cities[start].king = 1;
    game.campaign.cities[dest[0]].king = 0;
    for(int g = 0; g < 2; ++g) {
        auto& o = game.campaign.officers[g];
        o.king = int8_t(g == 0 ? 1 : 0); o.city = int16_t(g == 0 ? start : dest[0]); o.status = sg::Status::employed;
    }
    game.campaign.phase = sg::Phase::world;
    game.campaign.officers[0].force = 0;
    game.campaign.officers[1].force = 2;
    game.campaign.cities[dest[0]].defense = 550;
    int attacker = 0;
    assert(game.send_army(start,dest[0],&attacker,1));
    assert(start==5 && dest[0]!=0 && ((sg::city_neighbors[start]>>0)&1)==0);
    const int moving_army=game.campaign.officers[attacker].army;
    assert(game.redirect_army(moving_army,0));
    assert(game.campaign.armies[moving_army].target==0 && game.valid());
    assert(game.redirect_army(moving_army,dest[0]));
    assert(game.campaign.armies[moving_army].target==dest[0] && game.valid());
    game.start_battle(0);
    assert(sg::battle_view_mirrored(game));
    assert(sg::battle_camera_pan_direction(game,6) == -6);
    game.campaign.player = uint8_t(game.campaign.officers[game.battle.sides[1].general].king);
    assert(!sg::battle_view_mirrored(game));
    assert(sg::battle_camera_pan_direction(game,6) == 6);
    game.campaign.player = 1;
    // A new matchup must not inherit the previous generals' casting lock,
    // regeneration, immunity, field callbacks or pause state.
    game.battle.effect_remaining = 180;
    game.battle.effect_ticks = 90;
    game.battle.effect_script = 297;
    game.battle.effect_applied = game.battle.paused = 1;
    game.battle.heal_wait = 8;
    game.battle.heal_steps = game.battle.heal_remainder = 5;
    game.battle.immunity_ticks[0] = game.battle.immunity_ticks[1] = 100;
    game.battle.magic_callback_damage = 30;
    game.battle.magic_field_iterations = game.battle.magic_field_wait = game.battle.magic_field_state = 3;
    game.battle.projectiles[0].active = 1;
    const int previous_battle_tick = game.battle.tick;
    game.start_duel();
    assert(game.battle.tick == previous_battle_tick);
    assert(!game.battle.effect_remaining && !game.battle.effect_ticks && !game.battle.effect_script);
    assert(!game.battle.effect_applied && !game.battle.paused);
    assert(!game.battle.heal_wait && !game.battle.heal_steps && !game.battle.heal_remainder);
    assert(!game.battle.immunity_ticks[0] && !game.battle.immunity_ticks[1]);
    assert(!game.battle.magic_callback_damage && !game.battle.magic_field_iterations &&
           !game.battle.magic_field_wait && !game.battle.magic_field_state);
    assert(!game.battle.projectiles[0].active);
    assert(game.battle.terrain == sg::original::terrain_at(sg::city_defs[dest[0]].x,sg::city_defs[dest[0]].y));
    assert(game.battle.sides[0].side_bonus == 0);
    assert(game.battle.sides[1].side_bonus == game.campaign.cities[dest[0]].defense);
    assert(game.battle.sides[0].soldier_hit_threshold == sg::original::soldier_hit_threshold(
        game.battle.sides[0].force,game.battle.sides[1].force,game.battle.terrain,game.battle.sides[0].side_bonus));
    assert(game.battle.sides[1].soldier_hit_threshold == sg::original::soldier_hit_threshold(
        game.battle.sides[1].force,game.battle.sides[0].force,game.battle.terrain,game.battle.sides[1].side_bonus));
    // Exercise actual orders for both orientations and melee/ranged troops.
    // Empty off-center lanes must advance, not mistake a sentinel for a target.
    const sg::Order tested_orders[]={sg::Order::hold,sg::Order::advance,sg::Order::front,sg::Order::rear,sg::Order::general,sg::Order::front_back};
    const int tested_forces[]={0,2,4};
    for(int side=0;side<2;++side) for(int force : tested_forces) for(auto order : tested_orders) {
        game.start_duel(); game.battle.tick=0;
        for(auto& soldier : game.battle.soldiers) soldier.hp=0;
        for(int s=0;s<2;++s) {
            auto& fighter=game.battle.sides[s];
            fighter.x=fighter.previous_x=int16_t(s ? 400 : 1600);
            fighter.order=sg::Order::hold;
            fighter.soldiers=fighter.front=1;
            fighter.mp=0;
        }
        const int direction=side ? 1 : -1;
        auto& own=game.battle.sides[side];
        own.soldiers=4; own.front=2; own.force=uint8_t(force);
        const int commander_start=own.x;
        int start_x[4], start_y[4];
        for(int i=0;i<4;++i) {
            auto& soldier=game.battle.soldiers[side*200+i];
            soldier.hp=1; soldier.side=uint8_t(side); soldier.index=uint8_t(i); soldier.rear=uint8_t(i%2);
            soldier.formation_column=uint8_t(i<2 ? 1 : 2);
            soldier.x=soldier.previous_x=int16_t(own.x+direction*(i<2 ? 120 : -80));
            soldier.y=soldier.previous_y=int16_t(i%2 ? 240 : 120);
            soldier.move_ticks=0; soldier.attack_ticks=0; soldier.action_ticks=1000;
            start_x[i]=soldier.x; start_y[i]=soldier.y;
        }
        assert(game.issue_battle_order(side,order));
        for(int tick=0;tick<120;++tick) {
            game.battle.sides[0].charge=game.battle.sides[1].charge=0;
            game.battle_tick();
        }
        const bool general_moves=order==sg::Order::advance || order==sg::Order::general;
        assert(general_moves ? (own.x-commander_start)*direction>0 : own.x==commander_start);
        for(int i=0;i<4;++i) {
            const auto& soldier=game.battle.soldiers[side*200+i];
            const bool moves=order==sg::Order::advance || (order==sg::Order::front && i<2) || (order==sg::Order::rear && i>=2);
            if(order==sg::Order::front_back && i<2) assert((soldier.x-start_x[i])*direction<0);
            else assert(moves ? (soldier.x-start_x[i])*direction>0 : soldier.x==start_x[i] && soldier.y==start_y[i]);
        }
    }
    // demake's formation commands move the front rank away from, or into,
    // the center row. Keep the command-to-action mapping covered separately
    // from the low-level order API used by AI and saves.
    for(auto command : {sg::BattleCommand::disperse,sg::BattleCommand::fall_in}) {
        game.start_duel(); game.battle.tick=0;
        for(auto& soldier : game.battle.soldiers) soldier.hp=0;
        for(int side=0;side<2;++side) {
            auto& fighter=game.battle.sides[side];
            fighter.hp=1000; fighter.x=fighter.previous_x=int16_t(side ? 1800 : 400);
            fighter.soldiers=fighter.front=1; fighter.mp=0;
        }
        auto& own=game.battle.sides[0]; own.soldiers=own.front=2;
        for(int i=0;i<2;++i) {
            auto& soldier=game.battle.soldiers[i];
            soldier.hp=1; soldier.side=0; soldier.index=uint8_t(i); soldier.rear=0;
            soldier.formation_column=1;
            soldier.x=soldier.previous_x=int16_t(500+i*80);
            soldier.y=soldier.previous_y=int16_t(i ? 210 : 150);
            soldier.move_ticks=0;
        }
        assert(game.issue_battle_command(0,command));
        assert(game.battle.soldiers[0].move_ticks==32 && game.battle.soldiers[1].move_ticks==32);
        const int expected_first=command==sg::BattleCommand::disperse ? 120 : 180;
        const int expected_second=command==sg::BattleCommand::disperse ? 240 : 180;
        assert(game.battle.soldiers[0].move_target_y==expected_first);
        assert(game.battle.soldiers[1].move_target_y==expected_second);
    }
    game.start_duel(); game.battle.tick=0;
    // Spell economy: ordinary battle ticks never create MP or cast for the
    // player. A full gauge takes attribute-based time and each cast is finite.
    auto& caster=game.battle.sides[0];
    auto& spell_opponent=game.battle.sides[1];
    const auto* basic_spell=sg::skill_by_id(game.learned_skill(caster.general,0));
    assert(basic_spell && basic_spell->mp>0 && basic_spell->power==100);
    caster.mp=int16_t(basic_spell->mp*2); spell_opponent.mp=0;
    caster.charge=spell_opponent.charge=0;
    caster.order=spell_opponent.order=sg::Order::hold;
    caster.soldiers=caster.front=spell_opponent.soldiers=spell_opponent.front=1;
    spell_opponent.hp=10000; // Isolate casting economy from victory settlement.
    for(auto& soldier : game.battle.soldiers) soldier.hp=0;
    const int charge_gain=sg::original::battle_charge_increment(game.strength(caster.general));
    const int recharge_ticks=(sg::original::cast_charge_max+charge_gain-1)/charge_gain;
    for(int tick=0;tick<recharge_ticks+120;++tick) game.battle_tick();
    assert(caster.charge==sg::original::cast_charge_max && caster.mp==basic_spell->mp*2);
    assert(spell_opponent.mp==0);
    assert(game.cast_skill(0,0));
    assert(caster.mp==basic_spell->mp && caster.charge==0);
    assert(!game.cast_skill(0,0));
    game.battle_tick();
    assert(caster.charge==unsigned(charge_gain) && caster.mp==basic_spell->mp);
    for(int tick=1;tick<recharge_ticks;++tick) game.battle_tick();
    assert(caster.mp==basic_spell->mp && caster.charge==sg::original::cast_charge_max);
    assert(game.cast_skill(0,0));
    assert(caster.mp==0);
    caster.charge=sg::original::cast_charge_max;
    game.battle.effect_remaining=0;
    assert(!game.cast_skill(0,0));
    // Enemy AI chooses a learned skill, rather than a random global-table entry.
    spell_opponent.mp=200; spell_opponent.charge=sg::original::cast_charge_max;
    spell_opponent.hp=1; spell_opponent.soldiers=spell_opponent.front=0;
    game.battle.duel_tick=59;
    game.battle_tick();
    assert(spell_opponent.mp<200);
    bool known_ai_spell=false;
    for(int slot=0;slot<game.skill_count(spell_opponent.general);++slot) {
        const auto* learned=sg::skill_by_id(game.learned_skill(spell_opponent.general,slot));
        known_ai_spell |= learned && learned->script==game.battle.effect_script;
    }
    assert(known_ai_spell);
    game.start_duel();
    game.start_duel();
    // demake GeneralController clamps the STR difference used by a front-hit
    // parry check to [-20,20].  Pick the strongest and weakest officers so a
    // bounded threshold from the ATTACKER minus DEFENDER difference.
    int strongest=0, weakest=0;
    for(int g=1;g<sg::general_count;++g) {
        if(game.strength(g)>game.strength(strongest)) strongest=g;
        if(game.strength(g)<game.strength(weakest)) weakest=g;
    }
    auto& clamp_attacker=game.battle.sides[0];
    auto& clamp_defender=game.battle.sides[1];
    clamp_attacker.general=int16_t(strongest);
    clamp_defender.general=int16_t(weakest);
    clamp_attacker.x=1020; clamp_defender.x=1000;
    clamp_attacker.hp=clamp_defender.hp=1000;
    clamp_attacker.soldiers=clamp_attacker.front=0;
    clamp_defender.soldiers=clamp_defender.front=0;
    clamp_attacker.ai_action_flags=clamp_defender.ai_action_flags=0;
    for(auto& soldier:game.battle.soldiers) soldier.hp=0;
    const int raw_difference=game.strength(clamp_attacker.general)-game.strength(clamp_defender.general);
    assert(raw_difference > 20);
    const int clamped_threshold=60+sg::clamp(raw_difference,-20,20);
    sg::Game clamp_probe=game;
    uint32_t rejected_seed=1;
    for(;rejected_seed<1000000;++rejected_seed) {
        clamp_probe.campaign.random=rejected_seed;
        clamp_probe.campaign.random_secondary=0x87654321;
        const int draw=clamp_probe.roll(100);
        if(draw>clamped_threshold && draw<=60+raw_difference) break;
    }
    assert(rejected_seed<1000000);
    clamp_probe=game;
    clamp_probe.campaign.random=rejected_seed;
    clamp_probe.campaign.random_secondary=0x87654321;
    demake_general_apply_hit(clamp_probe,0,-1);
    assert(clamp_probe.battle.sides[1].hp==1000); // Clamp must still permit parry.
    uint32_t clamp_seed=1;
    for(;clamp_seed<1000000;++clamp_seed) {
        clamp_probe.campaign.random=clamp_seed;
        clamp_probe.campaign.random_secondary=0x87654321;
        if(clamp_probe.roll(100)<=clamped_threshold) break;
    }
    assert(clamp_seed<1000000);
    game.campaign.random=clamp_seed;
    game.campaign.random_secondary=0x87654321;
    const int clamp_hp=clamp_defender.hp;
    demake_general_apply_hit(game,0,-1);
    assert(clamp_defender.hp<clamp_hp);
    // The same accepted hit uses demake's separate mighty-hit interval when
    // the front-attack state is latched, consuming RNG(5) instead of RNG(6).
    sg::Game normal_hit=game;
    sg::Game mighty_hit=game;
    normal_hit.campaign.random=clamp_seed;
    normal_hit.campaign.random_secondary=0x87654321;
    mighty_hit.campaign.random=clamp_seed;
    mighty_hit.campaign.random_secondary=0x87654321;
    normal_hit.battle.sides[1].hp=mighty_hit.battle.sides[1].hp=1000;
    mighty_hit.battle.sides[0].ai_action_flags=sg::fighter_mighty_hit_flag;
    const int attack_strength=normal_hit.strength(normal_hit.battle.sides[0].general);
    sg::Game normal_expected=normal_hit;
    sg::Game mighty_expected=mighty_hit;
    const int expected_hit_roll=normal_expected.roll(100);
    assert(expected_hit_roll<=clamped_threshold);
    const int expected_normal=attack_strength/12-3+normal_expected.roll(6);
    assert(mighty_expected.roll(100)<=clamped_threshold);
    const int expected_mighty=attack_strength/12+mighty_expected.roll(5);
    demake_general_apply_hit(normal_hit,0,-1);
    demake_general_apply_hit(mighty_hit,0,-1);
    assert(normal_hit.battle.sides[1].hp==1000-expected_normal);
    assert(mighty_hit.battle.sides[1].hp==1000-expected_mighty);
    assert(expected_mighty>=attack_strength/12 && expected_mighty<=attack_strength/12+4);
    int initial_mp = game.battle.sides[0].mp;
    assert(!game.cast_skill(0,7));
    assert(game.battle.sides[0].mp == initial_mp);
    auto& mounted = game.battle.sides[0];
    auto& defending = game.battle.sides[1];
    mounted.order = sg::Order::general;
    mounted.x = 1000; mounted.previous_x = mounted.x; mounted.soldiers = 1; mounted.front = 1;
    defending.order = sg::Order::hold;
    defending.x = 1400; defending.previous_x = defending.x; defending.soldiers = 1; defending.front = 1;
    // This fixture tests lane hits, not the delayed opening/tactical AI.
    defending.ai_action_flags|=sg::fighter_opening_finished_flag|sg::fighter_opening_checked_flag;
    game.battle.ai_decision_countdown[1]=60000;
    for(auto& soldier : game.battle.soldiers) soldier.hp = 0;
    auto& target = game.battle.soldiers[200];
    target.hp = 1; target.x = 1020; target.y = sg::battle_commander_y; target.side = 1; target.index = 0; target.rear = 0;
    game.battle.tick = 0; game.battle.duel_tick = 0;
    game.battle.immunity_ticks[0] = 120; // Keep the test focused on mounted general vs. soldier.
    game.campaign.random = 12345;
    game.campaign.random_secondary = 0x87654321;
    for(int tick = 0; tick < 1800 && defending.soldiers > 0; ++tick) game.battle_tick();
    assert(target.hp == 0 && defending.soldiers == 0);
    // Chase and clear a mixed group ahead of and behind the mounted general.
    const int offsets[] = {-40,-40,0,0,40,40};
    const int rows[] = {150,210,180,150,210,180};
    defending.soldiers = defending.front = 6;
    defending.mp = 0;
    for(int i=0;i<6;++i) {
        auto& soldier=game.battle.soldiers[200+i];
        soldier.hp=1; soldier.x=int16_t(mounted.x+offsets[i]);
        soldier.y=int16_t(rows[i]); soldier.side=1; soldier.index=uint8_t(i); soldier.rear=0;
    }
    game.battle.immunity_ticks[0]=2000;
    game.campaign.random=12345;
    for(int tick=0;tick<1800 && defending.soldiers>0;++tick) game.battle_tick();
    // demake's mounted-general scan attacks the exact occupied row. Adjacent
    // rows stay alive until soldiers target them or enter the commander's row.
    assert(defending.soldiers==4 && defending.front==4);
    for(int i=0;i<6;++i)
        assert(game.battle.soldiers[200+i].hp==(rows[i]==sg::battle_commander_y ? 0 : 1));
    // At two cells' distance, holding commanders must neither swing nor hit.
    // Moving commanders must close to one cell before exchanging real damage.
    game.start_duel();
    mounted.x = mounted.previous_x = 1160;
    defending.x = defending.previous_x = 1080;
    mounted.order = defending.order = sg::Order::hold;
    mounted.attack_ticks = defending.attack_ticks = 0;
    mounted.soldiers = mounted.front = defending.soldiers = defending.front = 1;
    for(auto& soldier : game.battle.soldiers) soldier.hp = 0;
    const int mounted_hp = mounted.hp, defending_hp = defending.hp;
    game.battle.tick = 0;
    for(int tick=0;tick<120;++tick) {
        mounted.mp = defending.mp = 0;
        mounted.charge = defending.charge = 0;
        game.battle_tick();
        assert(!mounted.attack_ticks && !defending.attack_ticks);
        assert(mounted.hp==mounted_hp && defending.hp==defending_hp);
    }
    mounted.x = mounted.previous_x = 1200;
    defending.x = defending.previous_x = 1040;
    mounted.order = defending.order = sg::Order::general;
    mounted.soldiers = mounted.front = defending.soldiers = defending.front = 0;
    for(int tick=0;tick<1000 && game.battle.pending_winner<0 &&
        (mounted.hp==mounted_hp || defending.hp==defending_hp);++tick) {
        mounted.mp = defending.mp = 0;
        mounted.charge = defending.charge = 0;
        game.battle_tick();
        if(mounted.x-defending.x>62)
            assert(!mounted.attack_ticks && !defending.attack_ticks);
    }
    assert(mounted.hp < mounted_hp && defending.hp < defending_hp);
    assert(mounted.x-defending.x>0 && mounted.x-defending.x<=62);
    assert(game.battle.general_hit_ticks[0] || game.battle.general_hit_ticks[1]);
    game.battle.sides[1].hp = 0;
    game.resolve_duel(0);
    assert(game.campaign.cities[dest[0]].king == 1);
    assert(game.campaign.phase == sg::Phase::ended || game.campaign.armies[0].active == 1);
    assert(game.campaign.officers[0].city == dest[0]);
    if(game.campaign.phase == sg::Phase::prisoners) game.finish_prisoners();
    assert(game.campaign.phase == sg::Phase::ended);
    stable();

    // Native 0x40d4e8 / 0x40d804 field encounters pit two opposing armies
    // on a shared link without capturing the city at the crossing point.
    game.new_game(1,1);
    for(int i = 0; i < sg::city_count; ++i) {
        game.campaign.cities[i].king = -1;
        game.campaign.cities[i].major = -1;
    }
    for(auto& o : game.campaign.officers) {
        o.status = sg::Status::absent; o.king = -1; o.city = -1; o.army = -1;
    }
    const int field_start = 5;
    int field_neighbors[sg::city_count];
    assert(game.neighbors(field_start,field_neighbors) > 0);
    const int field_other = field_neighbors[0];
    game.campaign.cities[field_start].king = 1;
    game.campaign.cities[field_other].king = 0;
    game.campaign.officers[0].status = sg::Status::employed;
    game.campaign.officers[0].king = 1;
    game.campaign.officers[0].city = int16_t(field_start);
    game.campaign.officers[1].status = sg::Status::employed;
    game.campaign.officers[1].king = 0;
    game.campaign.officers[1].city = int16_t(field_other);
    game.campaign.phase = sg::Phase::world;
    assert(game.send_army(field_start,field_other,&attacker,1));
    int opponent = 1;
    assert(game.send_army(field_other,field_start,&opponent,1));
    for(int tick = 0; tick < 1000 && game.campaign.phase == sg::Phase::world; ++tick) game.world_tick();
    assert(game.campaign.phase == sg::Phase::battle);
    assert(game.battle.field_battle && game.battle.army == 0 && game.battle.opponent_army == 1);
    assert(game.battle.city == -1);
    const int field_x = (sg::city_defs[field_start].x + sg::city_defs[field_other].x) / 2;
    const int field_y = (sg::city_defs[field_start].y + sg::city_defs[field_other].y) / 2;
    assert(game.battle.terrain == sg::original::terrain_at(field_x,field_y));
    assert(game.battle.sides[0].soldier_hit_threshold == sg::original::soldier_hit_threshold(
        game.battle.sides[0].force,game.battle.sides[1].force,game.battle.terrain,0));
    assert(game.battle.sides[0].side_bonus == 0 && game.battle.sides[1].side_bonus == 0);
    assert(game.campaign.cities[field_start].king == 1 && game.campaign.cities[field_other].king == 0);
    game.battle.captured[0] = 0;
    game.battle.captured_count = 1;
    game.campaign.officers[0].status = sg::Status::prisoner;
    game.campaign.officers[0].king = 0;
    game.campaign.officers[0].army = -1;
    game.campaign.officers[0].city = -1;
    game.battle.sides[1].hp = 0;
    game.resolve_duel(0);
    assert(game.campaign.armies[0].active == 1);
    assert(!game.campaign.armies[1].active || game.campaign.armies[1].retreating);
    assert(game.campaign.officers[0].status == sg::Status::employed && game.campaign.officers[0].army == 0);
    assert(game.campaign.cities[field_start].king == 1 && game.campaign.cities[field_other].king == 0);
    if(game.campaign.phase == sg::Phase::prisoners) game.finish_prisoners();
    stable();

    // Native 0x421407 bases recruited experience on the top third of the
    // recruiting faction, then applies 90..119% of three quarters of that
    // average. Brute-force a seed whose search rolls select the local city and
    // its sole wild officer so the result can be compared exactly.
    game.new_game(1,1);
    for(int i = 0; i < sg::city_count; ++i) {
        game.campaign.cities[i].king = -1;
        game.campaign.cities[i].major = -1;
    }
    for(auto& o : game.campaign.officers) {
        o.status = sg::Status::absent; o.king = -1; o.city = -1; o.army = -1;
    }
    const int recruit_city = 5;
    game.campaign.cities[recruit_city].king = 1;
    const int ruler = sg::king_defs[1][1].general;
    int recruiter = -1;
    for(int g = 0; g < sg::general_count; ++g)
        if(g != ruler && (sg::general_defs[g].str >= 85 || sg::general_defs[g].intel >= 85)) { recruiter = g; break; }
    assert(recruiter >= 0);
    int roster[4] = {recruiter,-1,-1,-1}, roster_count = 1;
    for(int g = 0; g < sg::general_count && roster_count < 4; ++g) {
        if(g == ruler || g == recruiter) continue;
        roster[roster_count++] = g;
    }
    assert(roster_count == 4);
    for(int i = 0; i < 4; ++i) {
        auto& o = game.campaign.officers[roster[i]];
        o.status = sg::Status::employed; o.king = 1; o.city = int16_t(recruit_city); o.army = -1;
        o.exp = uint32_t(10000 - i * 1000); o.worked = 0;
    }
    int wild = -1;
    for(int g = 0; g < sg::general_count; ++g) {
        bool used = false;
        for(int i = 0; i < 4; ++i) used |= roster[i] == g;
        if(g != ruler && !used && (sg::general_defs[g].intel >= 80 || sg::general_defs[g].str >= 80)) { wild = g; break; }
    }
    assert(wild >= 0);
    auto& candidate = game.campaign.officers[wild];
    candidate.status = sg::Status::wild; candidate.king = -1; candidate.city = int16_t(recruit_city);
    candidate.army = -1; candidate.exp = 0; candidate.rank = 0;
    int8_t changed_unowned[sg::general_count]{};
    for(int g=0;g<sg::general_count;++g) if(g!=wild && game.campaign.officers[g].king==-1) {
        changed_unowned[g]=1;
        game.campaign.officers[g].king=1;
    }
    // Native 0x421407 recomputes a recruit's experience from the recruiting
    // faction's top third by EXP, scaled by 3/4 and a 90..119% roll -- not from
    // the destination city's garrison. Capture that reference before the call.
    int faction_exp[sg::general_count], faction_count=0;
    for(int g=0;g<sg::general_count;++g) {
        const auto& officer=game.campaign.officers[g];
        if(officer.status==sg::Status::employed && officer.king==1) faction_exp[faction_count++]=int(officer.exp);
    }
    assert(faction_count>0);
    for(int i=0;i<faction_count;++i) for(int j=i+1;j<faction_count;++j)
        if(faction_exp[j]>faction_exp[i]) { const int swap=faction_exp[i]; faction_exp[i]=faction_exp[j]; faction_exp[j]=swap; }
    const int top_count=sg::maximum(1,faction_count/3);
    uint64_t top_total=0;
    for(int i=0;i<sg::minimum(faction_count,top_count);++i) top_total+=uint32_t(faction_exp[i]);
    const uint32_t recruit_base=uint32_t(top_total/unsigned(top_count))*3/4;
    auto experience_for_level=[](int target_level) -> uint32_t {
        if(target_level==2) return 200;
        if(target_level==3) return 377;
        if(target_level==4) return 771;
        if(target_level<50) {
            const int adjusted=target_level-5;
            return uint32_t(1302+531*adjusted+137*(adjusted+1)*adjusted/2);
        }
        return 99999;
    };
    uint32_t recruit_secondary = 0x87654321;
    uint32_t recruit_seed = 1;
    for(; recruit_seed < 1000000; ++recruit_seed) {
        uint32_t primary = recruit_seed, secondary = recruit_secondary;
        if(next_random(primary,secondary,100)/20 != 1) continue;
        (void)next_random(primary,secondary,1);
        if(next_random(primary,secondary,100)/50 == 0) break;
    }
    assert(recruit_seed < 1000000);
    game.campaign.random = recruit_seed;
    game.campaign.random_secondary = recruit_secondary;
    assert(game.domestic_action(recruit_city,recruiter,2));
    assert(candidate.status == sg::Status::employed && candidate.king == 1 && candidate.city == recruit_city);
    for(int g=0;g<sg::general_count;++g) if(changed_unowned[g]) game.campaign.officers[g].king=-1;
    assert(game.search_result_general == wild && game.search_result_city == recruit_city);
    assert(candidate.exp >= recruit_base*90/100 && candidate.exp <= recruit_base*119/100);
    int expected_rank = 0;
    while(expected_rank < sg::original::maximum_level && candidate.exp >= experience_for_level(expected_rank+1)) ++expected_rank;
    assert(candidate.rank == expected_rank);
    assert(candidate.front <= game.front_capacity(wild) && candidate.front <= candidate.soldiers);
    assert(candidate.soldiers <= game.max_soldiers(wild));
    assert(candidate.hp <= game.max_hp(wild) && candidate.mp <= game.max_mp(wild));
    assert(candidate.worked == 1); // demake AddGeneralToCity sets active=0 for the new recruit.
    assert(!game.domestic_action(recruit_city,wild,sg::general_defs[wild].intel >= 80 ? 0 : 1));
    assert(candidate.worked == 1);
    stable();

    // demake searches the global list of unowned generals; a talent located
    // outside the searched city can still join the executing officer's city.
    game.new_game(1,1);
    for(auto& city : game.campaign.cities) { city.king=-1; city.major=-1; }
    for(auto& o : game.campaign.officers) { o.status=sg::Status::absent; o.king=-1; o.city=-1; o.army=-1; }
    const int retry_city=5;
    game.campaign.cities[retry_city].king=1;
    int retry_neighbors[sg::city_count];
    const int retry_neighbor_count=game.neighbors(retry_city,retry_neighbors);
    assert(retry_neighbor_count>0);
    const int retry_ruler=sg::king_defs[1][1].general;
    int retry_worker=-1,retry_target=-1;
    for(int g=0;g<sg::general_count;++g) {
        if(g!=retry_ruler && retry_worker<0 &&
           (sg::general_defs[g].str>=85 || sg::general_defs[g].intel>=85)) retry_worker=g;
        else if(g!=retry_ruler && g!=retry_worker && retry_target<0) retry_target=g;
    }
    assert(retry_worker>=0 && retry_target>=0);
    auto& retry_ruler_record=game.campaign.officers[retry_ruler];
    retry_ruler_record.status=sg::Status::employed; retry_ruler_record.king=1;
    retry_ruler_record.city=int16_t(retry_city); retry_ruler_record.army=-1;
    auto& retry_worker_record=game.campaign.officers[retry_worker];
    retry_worker_record.status=sg::Status::employed; retry_worker_record.king=1;
    retry_worker_record.city=int16_t(retry_city); retry_worker_record.army=-1;
    auto& retry_target_record=game.campaign.officers[retry_target];
    retry_target_record.status=sg::Status::wild; retry_target_record.king=-1;
    retry_target_record.city=int16_t(retry_neighbors[0]); retry_target_record.army=-1;
    for(int g=0;g<sg::general_count;++g)
        if(g!=retry_target && game.campaign.officers[g].king==-1) game.campaign.officers[g].king=1;
    uint32_t retry_seed=1;
    for(;retry_seed<1000000;++retry_seed) {
        uint32_t primary=retry_seed,secondary=0x87654321;
        if(next_random(primary,secondary,100)/20!=1) continue;
        (void)next_random(primary,secondary,1);
        if(next_random(primary,secondary,100)/50==0) break;
    }
    assert(retry_seed<1000000);
    game.campaign.random=retry_seed;
    game.campaign.random_secondary=0x87654321;
    assert(game.domestic_action(retry_city,retry_worker,2));
    assert(retry_target_record.status==sg::Status::employed && retry_target_record.city==retry_city);
    assert(game.search_result_general==retry_target && game.search_result_city==retry_city);
    stable();

    // Native search-result item selection reads 55 weighted records, stores
    // the selected ID on the searching officer, and suppresses repeat picks
    // for kind-2 records after 0x409a33 marks them used.
    game.new_game(1,1);
    sg::Officer packed_test{};
    for(uint8_t id=38;id<55;++id) assert(sg::append_inventory_item(packed_test,id));
    for(int i=0;i<17;++i) assert(sg::inventory_item(packed_test,i) == 38+i);
    sg::remove_inventory_item(packed_test,7);
    assert(packed_test.item_count == 16 && sg::inventory_item(packed_test,7) == 46 && sg::inventory_item(packed_test,15) == 54);
    for(auto& city : game.campaign.cities) { city.king = -1; city.major = -1; }
    for(auto& o : game.campaign.officers) { o.status = sg::Status::absent; o.king = -1; o.city = -1; o.army = -1; }
    const int item_city = 5;
    game.campaign.cities[item_city].king = 1;
    int item_workers[2] = {-1,-1};
    int item_worker_count = 0;
    for(int g = 0; g < sg::general_count && item_worker_count < 2; ++g) {
        if(g == ruler || (sg::general_defs[g].str < 85 && sg::general_defs[g].intel < 85)) continue;
        item_workers[item_worker_count++] = g;
    }
    assert(item_worker_count == 2);
    for(int g : item_workers) {
        auto& o = game.campaign.officers[g];
        o.status = sg::Status::employed; o.king = 1; o.city = int16_t(item_city); o.army = -1; o.worked = 0;
    }
    // demake stores search results in the executing general's city as typed
    // objects, without consuming a unique-item flag or the officer's backpack.
    auto seed_for_band=[&](int band,int result_count) {
        for(uint32_t seed=1;seed<1000000;++seed) {
            uint32_t primary=seed,secondary=0x87654321;
            if(next_random(primary,secondary,100)/20!=band) continue;
            const int result=next_random(primary,secondary,result_count);
            return std::pair<uint32_t,int>{seed,result};
        }
        assert(false);
        return std::pair<uint32_t,int>{0,-1};
    };
    int search_strength_before[2],search_intelligence_before[2],search_equipped_before[2];
    for(int i=0;i<2;++i) {
        search_strength_before[i]=game.strength(item_workers[i]);
        search_intelligence_before[i]=game.intelligence(item_workers[i]);
        search_equipped_before[i]=game.campaign.officers[item_workers[i]].equipped_item;
    }
    auto [equipment_seed,equipment_index]=seed_for_band(2,31);
    game.campaign.random=equipment_seed; game.campaign.random_secondary=0x87654321;
    assert(game.domestic_action(item_city,item_workers[0],2));
    assert(sg::city_inventory_count(game.campaign,item_city)==1);
    assert(sg::city_inventory_type(sg::city_inventory_item(game.campaign,item_city,0))==1);
    assert(sg::city_inventory_index(sg::city_inventory_item(game.campaign,item_city,0))==equipment_index);
    assert(game.campaign.officers[item_workers[0]].item_count==0);
    assert(game.campaign.item_used[equipment_index]==0);

    auto [arms_seed,arms_index]=seed_for_band(3,11);
    game.campaign.officers[item_workers[1]].worked=0;
    game.campaign.random=arms_seed; game.campaign.random_secondary=0x87654321;
    assert(game.domestic_action(item_city,item_workers[1],2));
    const uint8_t arms_entry=sg::city_inventory_item(game.campaign,item_city,1);
    assert(sg::city_inventory_type(arms_entry)==2 && sg::city_inventory_index(arms_entry)==arms_index);

    // The third object band stores a formation object in the same city pool.
    game.campaign.officers[item_workers[0]].worked=0;
    auto [formation_seed,formation_index]=seed_for_band(4,7);
    game.campaign.random=formation_seed; game.campaign.random_secondary=0x87654321;
    assert(game.domestic_action(item_city,item_workers[0],2));
    const uint8_t formation_entry=sg::city_inventory_item(game.campaign,item_city,2);
    assert(sg::city_inventory_type(formation_entry)==3 && sg::city_inventory_index(formation_entry)==formation_index);
    assert(game.campaign.officers[item_workers[0]].item_count==0);
    // Finding equipment, a troop token or a formation book must not apply it.
    for(int i=0;i<2;++i) {
        assert(game.strength(item_workers[i])==search_strength_before[i]);
        assert(game.intelligence(item_workers[i])==search_intelligence_before[i]);
        assert(game.campaign.officers[item_workers[i]].equipped_item==search_equipped_before[i]);
    }

    // City object use equips/swaps/returns equipment and consumes arms and
    // formations while preserving the current officers' permanent stats.
    auto& item_officer=game.campaign.officers[item_workers[0]];
    while(sg::city_inventory_count(game.campaign,item_city))
        sg::remove_city_inventory_item(game.campaign,item_city,sg::city_inventory_count(game.campaign,item_city)-1);
    item_officer.equipped_item=0;
    item_officer.hp=uint16_t(game.max_hp(item_workers[0]));
    item_officer.mp=uint16_t(game.max_mp(item_workers[0]));
    const int base_strength=game.strength(item_workers[0]);
    const int base_intelligence=game.intelligence(item_workers[0]);
    assert(sg::append_city_inventory_item(game.campaign,item_city,1,0)); // item 12
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.equipped_item==12 && sg::city_inventory_count(game.campaign,item_city)==0);
    assert(game.intelligence(item_workers[0])==base_intelligence+10);
    assert(sg::append_city_inventory_item(game.campaign,item_city,1,8)); // item 20
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.equipped_item==20 && sg::city_inventory_count(game.campaign,item_city)==1);
    assert(sg::city_inventory_type(sg::city_inventory_item(game.campaign,item_city,0))==1);
    assert(sg::city_inventory_index(sg::city_inventory_item(game.campaign,item_city,0))==0);
    assert(game.strength(item_workers[0])==base_strength+10);
    assert(game.intelligence(item_workers[0])==base_intelligence);
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.equipped_item==12 && sg::city_inventory_count(game.campaign,item_city)==1);
    assert(game.use_city_object(item_workers[0],item_city,-1));
    assert(item_officer.equipped_item==0 && sg::city_inventory_count(game.campaign,item_city)==2);
    assert(game.strength(item_workers[0])==base_strength && game.intelligence(item_workers[0])==base_intelligence);

    while(sg::city_inventory_count(game.campaign,item_city))
        sg::remove_city_inventory_item(game.campaign,item_city,sg::city_inventory_count(game.campaign,item_city)-1);
    // Replacements stay in the selected object's city and preserve its row;
    // an explicit unequip returns the worn item to the general's city.
    const int item_officer_city=(item_city+1)%sg::city_count;
    game.campaign.cities[item_officer_city].king=1;
    item_officer.city=item_officer_city;
    assert(sg::append_city_inventory_item(game.campaign,item_city,1,0));
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.equipped_item==12);
    for(int i=0;i<sg::city_object_capacity;++i)
        assert(sg::append_city_inventory_item(game.campaign,item_city,1,i==17 ? 8 : 1));
    assert(game.use_city_object(item_workers[0],item_city,17));
    assert(item_officer.equipped_item==20);
    assert(sg::city_inventory_count(game.campaign,item_city)==sg::city_object_capacity);
    assert(sg::city_inventory_index(sg::city_inventory_item(game.campaign,item_city,16))==1);
    assert(sg::city_inventory_index(sg::city_inventory_item(game.campaign,item_city,17))==0);
    assert(sg::city_inventory_index(sg::city_inventory_item(game.campaign,item_city,18))==1);
    assert(sg::city_inventory_count(game.campaign,item_officer_city)==0);
    assert(game.use_city_object(item_workers[0],item_city,-1));
    assert(item_officer.equipped_item==0);
    assert(sg::city_inventory_count(game.campaign,item_city)==sg::city_object_capacity);
    assert(sg::city_inventory_count(game.campaign,item_officer_city)==1);
    assert(sg::city_inventory_type(sg::city_inventory_item(game.campaign,item_officer_city,0))==1);
    assert(sg::city_inventory_index(sg::city_inventory_item(game.campaign,item_officer_city,0))==8);
    while(sg::city_inventory_count(game.campaign,item_city))
        sg::remove_city_inventory_item(game.campaign,item_city,sg::city_inventory_count(game.campaign,item_city)-1);
    sg::remove_city_inventory_item(game.campaign,item_officer_city,0);
    game.campaign.cities[item_officer_city].king=-1;
    item_officer.city=item_city;

    item_officer.forces=0; item_officer.formations=0;
    assert(sg::append_city_inventory_item(game.campaign,item_city,2,0));
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.forces==(1u<<sg::original::item_force_type(1)));
    assert(sg::append_city_inventory_item(game.campaign,item_city,2,0));
    assert(!game.use_city_object(item_workers[0],item_city,0));
    sg::remove_city_inventory_item(game.campaign,item_city,0);
    assert(sg::append_city_inventory_item(game.campaign,item_city,3,3));
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.formations==(1u<<3));
    assert(sg::append_city_inventory_item(game.campaign,item_city,3,3));
    assert(!game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.formations==(1u<<3));
    assert(sg::city_inventory_count(game.campaign,item_city)==1);
    assert(sg::city_inventory_type(sg::city_inventory_item(game.campaign,item_city,0))==3);
    assert(sg::city_inventory_index(sg::city_inventory_item(game.campaign,item_city,0))==3);

    // An absent general has no return city, so an explicit unequip falls back
    // to the selected source city, as demake's roster does.
    while(sg::city_inventory_count(game.campaign,item_city))
        sg::remove_city_inventory_item(game.campaign,item_city,sg::city_inventory_count(game.campaign,item_city)-1);
    item_officer.city=-1; item_officer.army=0;
    assert(sg::append_city_inventory_item(game.campaign,item_city,1,1));
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(item_officer.equipped_item==13 && sg::city_inventory_count(game.campaign,item_city)==0);
    assert(game.use_city_object(item_workers[0],item_city,-1));
    assert(item_officer.equipped_item==0 && sg::city_inventory_count(game.campaign,item_city)==1);
    item_officer.city=item_city; item_officer.army=-1;

    while(sg::city_inventory_count(game.campaign,item_city))
        sg::remove_city_inventory_item(game.campaign,item_city,sg::city_inventory_count(game.campaign,item_city)-1);
    const int base_max_hp=game.max_hp(item_workers[0]);
    assert(sg::append_city_inventory_item(game.campaign,item_city,1,6)); // item 18
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(game.max_hp(item_workers[0])==sg::minimum(999,base_max_hp+10));
    item_officer.hp=uint16_t(game.max_hp(item_workers[0]));
    assert(game.use_city_object(item_workers[0],item_city,-1));
    assert(item_officer.hp==base_max_hp && game.max_hp(item_workers[0])==base_max_hp);
    while(sg::city_inventory_count(game.campaign,item_city))
        sg::remove_city_inventory_item(game.campaign,item_city,sg::city_inventory_count(game.campaign,item_city)-1);
    const int base_max_mp=game.max_mp(item_workers[0]);
    assert(sg::append_city_inventory_item(game.campaign,item_city,1,5)); // item 17
    assert(game.use_city_object(item_workers[0],item_city,0));
    assert(game.max_mp(item_workers[0])==sg::minimum(999,base_max_mp+10));
    item_officer.mp=uint16_t(game.max_mp(item_workers[0]));
    assert(game.use_city_object(item_workers[0],item_city,-1));
    assert(item_officer.mp==base_max_mp && game.max_mp(item_workers[0])==base_max_mp);
    stable();

    game.new_game(1,1);
    game.campaign.month = 12;
    game.campaign.month_ticks = 299;
    game.campaign.phase = sg::Phase::world;
    game.campaign.officers[0].worked = 1;
    int annual_test_city = -1;
    int annual_cap_city = -1;
    int annual_player_city = -1;
    for(int i = 0; i < sg::city_count; ++i)
        if(game.campaign.cities[i].king >= 0 && game.campaign.cities[i].king != game.campaign.player) {
            if(annual_test_city < 0) annual_test_city = i;
            else { annual_cap_city = i; break; }
        }
    assert(annual_test_city >= 0);
    assert(annual_cap_city >= 0);
    game.campaign.cities[annual_test_city].money = 65530;
    game.campaign.cities[annual_cap_city].money = 999990;
    for(int i = 0; i < sg::city_count; ++i)
        if(game.campaign.cities[i].king == game.campaign.player) { annual_player_city = i; break; }
    assert(annual_player_city >= 0);
    game.campaign.cities[annual_player_city].money = 4321;
    uint32_t annual_money[sg::city_count], annual_population[sg::city_count];
    for(int i = 0; i < sg::city_count; ++i) {
        annual_money[i] = game.campaign.cities[i].money;
        annual_population[i] = game.campaign.cities[i].population;
        game.campaign.cities[i].reserve_ticks = 10;
    }
    int annual_ai_general=-1;
    for(int g=0;g<sg::general_count;++g) {
        const auto& officer=game.campaign.officers[g];
        if(officer.king>=0 && officer.king!=game.campaign.player && officer.status==sg::Status::employed) {
            annual_ai_general=g;
            break;
        }
    }
    assert(annual_ai_general>=0);
    // Native FUN_004032bb catch-up. It compares each AI officer against the
    // player's roster, so an AI officer already ahead of the player must gain
    // nothing. The previous placeholder granted every AI officer half a level
    // per year regardless, which this pair of assertions rejects.
    int annual_ahead=-1, annual_behind=-1;
    for(int g=0;g<sg::general_count && annual_behind<0;++g) {
        const auto& officer=game.campaign.officers[g];
        if(officer.status!=sg::Status::employed || officer.king==game.campaign.player || officer.king<0) continue;
        if(annual_ahead<0) annual_ahead=g; else annual_behind=g;
    }
    assert(annual_ahead>=0 && annual_behind>=0 && annual_ahead!=annual_behind);
    for(int g=0;g<sg::general_count;++g) {
        auto& officer=game.campaign.officers[g];
        if(officer.status!=sg::Status::employed) continue;
        officer.exp = officer.king==game.campaign.player ? 20000 : 90000;
    }
    // A zero faction ledger removes the 0x402d74 pass, isolating the
    // mean-EXP reference so the expected grants stay exact.
    for(auto& ledger : game.campaign.faction_experience) ledger=0;
    game.campaign.officers[annual_behind].exp=0;
    game.world_tick();
    assert(game.campaign.phase == sg::Phase::domestic);
    assert(game.campaign.year == 190 && game.campaign.month == 1);
    assert(game.campaign.officers[0].worked == 0);
    for(int i = 0; i < sg::city_count; ++i) {
        const bool receives_income = game.campaign.cities[i].king >= 0;
        const uint32_t expected_money = annual_money[i] +
            (receives_income ? sg::original::demake_city_annual_income(annual_population[i]) : 0);
        assert(game.campaign.cities[i].money == sg::minimum<uint32_t>(999999,expected_money));
    }
    assert(game.campaign.cities[annual_test_city].money > 65535);
    assert(game.campaign.cities[annual_cap_city].money == 999999);
    assert(game.campaign.cities[annual_player_city].money ==
           4321+sg::original::demake_city_annual_income(annual_population[annual_player_city]));
    assert(game.campaign.officers[annual_ahead].exp==90000);
    assert(game.campaign.officers[annual_behind].exp>=10000);
    stable();

    // demake's CheckIsLevelUp grows MP then HP maxima by independent 1–2 rolls.
    game.new_game(1,1);
    int levelup_general=0;
    auto& leveling=game.campaign.officers[levelup_general];
    leveling.rank=9;
    leveling.exp=6011;
    sg::set_officer_level_hp_cap(leveling,uint16_t(sg::original::max_hp(
        sg::general_defs[levelup_general].hp,game.strength(levelup_general),9)));
    sg::set_officer_level_mp_cap(leveling,uint16_t(sg::original::max_mp(
        sg::general_defs[levelup_general].mp,game.intelligence(levelup_general),9)));
    const int old_hp_cap=sg::officer_level_hp_cap(leveling);
    const int old_mp_cap=sg::officer_level_mp_cap(leveling);
    sg::Game levelup_rng=game;
    const int mp_growth=1+levelup_rng.roll(2);
    const int hp_growth=1+levelup_rng.roll(2);
    game.gain_experience(levelup_general,1);
    assert(leveling.rank==10 && leveling.exp==6012);
    assert(sg::officer_level_mp_cap(leveling)==old_mp_cap+mp_growth);
    assert(sg::officer_level_hp_cap(leveling)==old_hp_cap+hp_growth);
    stable();

    // demake AI promotion maps its 50 MagicConfig sequences to the imported
    // skill IDs. At level 10, spell 27 offers sequences 37/33/38.
    auto prepare_ai_promotion=[](sg::Game& target,int level) {
        target.new_game(1,1);
        int city=-1,king=-1;
        for(int i=0;i<sg::city_count;++i) {
            if(target.campaign.cities[i].king>=0 && target.campaign.cities[i].king!=target.campaign.player) {
                city=i; king=target.campaign.cities[i].king; break;
            }
        }
        assert(city>=0);
        auto& officer=target.campaign.officers[0];
        officer.king=int8_t(king);
        officer.status=sg::Status::employed;
        officer.city=int16_t(city);
        officer.army=-1;
        officer.rank=uint8_t(level);
        officer.front=uint16_t(target.front_capacity(0));
        officer.soldiers=uint16_t(target.max_soldiers(0));
        officer.learned[0]=58; // demake MagicConfig sequence 27.
        officer.learned[1]=0;
        officer.learned[2]=0;
        officer.learned[3]=0;
        target.campaign.phase=sg::Phase::domestic;
    };
    prepare_ai_promotion(game,10);
    game.finish_domestic();
    assert(game.campaign.officers[0].learned[1]==64 ||
           game.campaign.officers[0].learned[1]==68 ||
           game.campaign.officers[0].learned[1]==69);
    stable();
    prepare_ai_promotion(game,20);
    game.finish_domestic();
    assert(game.campaign.officers[0].learned[1]==68 ||
           game.campaign.officers[0].learned[1]==73 ||
           game.campaign.officers[0].learned[1]==78);
    assert(game.campaign.officers[0].learned[2]!=0);
    stable();
    prepare_ai_promotion(game,30);
    game.campaign.officers[0].learned[1]=64; // sequence 33
    game.campaign.officers[0].learned[2]=64;
    game.finish_domestic();
    assert(game.campaign.officers[0].learned[2]==74 ||
           game.campaign.officers[0].learned[2]==79 ||
           game.campaign.officers[0].learned[2]==75);
    stable();
    prepare_ai_promotion(game,40);
    game.campaign.officers[0].learned[1]=64;
    game.campaign.officers[0].learned[2]=64; // sequence 33 is an advanced spell.
    game.finish_domestic();
    assert(game.campaign.officers[0].learned[3]==81); // sequence 49.
    stable();

    // The player promotion UI uses the same demake spell tree as the AI, but
    // lets the player choose each of the three candidates.
    game.new_game(1,1);
    int player_promote=0;
    while(player_promote==sg::king_defs[1][1].general) ++player_promote;
    auto& player_candidate=game.campaign.officers[player_promote];
    player_candidate.king=1;
    player_candidate.status=sg::Status::employed;
    player_candidate.rank=20;
    player_candidate.learned[0]=sg::demake_initial_magic_skills[player_promote];
    player_candidate.learned[1]=0;
    player_candidate.learned[2]=0;
    player_candidate.learned[3]=0;
    const int selected_second=game.promotion_candidate_skill(player_promote,1,2);
    assert(selected_second!=0 && game.appoint_skill(player_promote,1,2));
    assert(player_candidate.learned[1]==selected_second);
    const int selected_third=game.promotion_candidate_skill(player_promote,2,0);
    assert(selected_third!=0 && game.appoint_skill(player_promote,2,0));
    assert(player_candidate.learned[2]==selected_third);
    player_candidate.rank=40;
    player_candidate.front=uint16_t(game.front_capacity(player_promote));
    player_candidate.soldiers=uint16_t(game.max_soldiers(player_promote));
    const int selected_final=game.promotion_candidate_skill(player_promote,3,0);
    assert(selected_final!=0 && game.appoint_skill(player_promote,3,0));
    assert(player_candidate.learned[3]==selected_final);
    {
        sg::Game royal;
        royal.new_game(1,1);
        const int ruler=sg::king_defs[1][1].general;
        auto& o=royal.campaign.officers[ruler];
        o.learned[1]=o.learned[2]=o.learned[3]=0;
        o.rank=9;
        assert(!royal.appoint_skill(ruler,1,0));
        o.rank=10;
        assert(royal.appoint_skill(ruler,1,0));
        assert(!royal.appoint_skill(ruler,1,0));
        assert(!royal.appoint_skill(ruler,2,0));
        o.rank=30;
        assert(royal.appoint_skill(ruler,2,0));
        o.rank=40;
        assert(royal.appoint_skill(ruler,3,0));
        assert(!royal.appoint_skill(ruler,3,0));
        o.learned[1]=0; o.status=sg::Status::prisoner;
        assert(!royal.appoint_skill(ruler,1,0));
    }
    stable();

    // demake resolves AI-versus-AI city attacks through AttackCity/WarResult,
    // not the native duel/equalized-troop resolver. Force a decisive AI win.
    sg::Game ai_city_battle;
    ai_city_battle.new_game(1,1);
    int attack_city=-1,target_city=-1,attacker_king=-1,defender_king=-1;
    for(int source=0;source<sg::city_count && attack_city<0;++source) {
        const int source_king=ai_city_battle.campaign.cities[source].king;
        if(source_king<0 || source_king==ai_city_battle.campaign.player) continue;
        for(int n=0;n<sg::city_neighbor_counts[source];++n) {
            const int target=sg::city_neighbor_order[source][n];
            const int target_king=ai_city_battle.campaign.cities[target].king;
            if(target_king>=0 && target_king!=ai_city_battle.campaign.player && target_king!=source_king) {
                attack_city=source; target_city=target; attacker_king=source_king; defender_king=target_king; break;
            }
        }
    }
    assert(attack_city>=0 && target_city>=0);
    int source_roster[sg::general_count],source_count=ai_city_battle.officers_at(attack_city,attacker_king,source_roster,sg::general_count);
    int target_roster[sg::general_count],target_count=ai_city_battle.officers_at(target_city,defender_king,target_roster,sg::general_count);
    assert(source_count>0 && target_count>0);
    const int ai_attacker=source_roster[0],ai_defender=target_roster[0];
    for(int i=1;i<target_count;++i) {
        auto& extra=ai_city_battle.campaign.officers[target_roster[i]];
        extra.status=sg::Status::absent; extra.city=-1;
    }
    auto& attacking=ai_city_battle.campaign.officers[ai_attacker];
    attacking.rank=49;
    attacking.front=uint16_t(ai_city_battle.front_capacity(ai_attacker));
    attacking.soldiers=uint16_t(ai_city_battle.max_soldiers(ai_attacker));
    attacking.hp=uint16_t(ai_city_battle.max_hp(ai_attacker));
    attacking.mp=uint16_t(ai_city_battle.max_mp(ai_attacker));
    attacking.city=-1;
    attacking.army=0;
    auto& ai_defending=ai_city_battle.campaign.officers[ai_defender];
    ai_defending.rank=1; ai_defending.soldiers=1; ai_defending.front=1; ai_defending.hp=1; ai_defending.mp=0;
    int third_party_prisoner=-1;
    for(int g=0;g<sg::general_count;++g) {
        auto& officer=ai_city_battle.campaign.officers[g];
        if(officer.status==sg::Status::employed && officer.king>=0 &&
           officer.king!=attacker_king && officer.king!=defender_king) {
            third_party_prisoner=g; officer.status=sg::Status::prisoner;
            officer.city=int16_t(target_city); officer.army=-1; break;
        }
    }
    assert(third_party_prisoner>=0);
    ai_city_battle.campaign.cities[target_city].major=int16_t(ai_defender);
    const uint32_t prior_population=ai_city_battle.campaign.cities[target_city].population;
    const uint16_t prior_defense=ai_city_battle.campaign.cities[target_city].defense;
    const uint32_t prior_money=ai_city_battle.campaign.cities[target_city].money;
    auto& moving=ai_city_battle.campaign.armies[0];
    moving={}; moving.active=1; moving.count=1; moving.king=int8_t(attacker_king);
    moving.origin=int8_t(attack_city); moving.target=int8_t(target_city); moving.total=10;
    moving.generals[0]=int16_t(ai_attacker); moving.money=77; moving.retreat_goal=-1;
    ai_city_battle.campaign.phase=sg::Phase::world;
    ai_city_battle.start_battle(0);
    assert(ai_city_battle.battle.winner==0);
    assert(ai_city_battle.campaign.phase==sg::Phase::world);
    assert(ai_city_battle.campaign.cities[target_city].king==attacker_king);
    assert(ai_city_battle.campaign.cities[target_city].population==prior_population);
    assert(ai_city_battle.campaign.cities[target_city].defense==prior_defense);
    assert(ai_city_battle.campaign.cities[target_city].money==prior_money+77);
    assert(!ai_city_battle.campaign.armies[0].active);
    assert(ai_defending.soldiers==0 && ai_defending.hp==2 && ai_defending.mp==2);
    assert(ai_city_battle.campaign.officers[third_party_prisoner].status==sg::Status::prisoner);
    assert(ai_city_battle.campaign.officers[third_party_prisoner].king==attacker_king);
    assert(ai_city_battle.campaign.officers[third_party_prisoner].city==target_city);
    assert(ai_city_battle.valid());

    // A defender who was captured in an earlier pairing is restored when the
    // defending city ultimately wins (demake clears prisonerIdx for all city generals).
    sg::Game ai_city_defense;
    ai_city_defense.new_game(1,1);
    attack_city=-1; target_city=-1; attacker_king=-1; defender_king=-1;
    for(int source=0;source<sg::city_count && attack_city<0;++source) {
        const int source_king=ai_city_defense.campaign.cities[source].king;
        if(source_king<0 || source_king==ai_city_defense.campaign.player) continue;
        for(int n=0;n<sg::city_neighbor_counts[source];++n) {
            const int target=sg::city_neighbor_order[source][n];
            const int target_king=ai_city_defense.campaign.cities[target].king;
            if(target_king>=0 && target_king!=ai_city_defense.campaign.player && target_king!=source_king) {
                int sr[sg::general_count],tr[sg::general_count];
                if(ai_city_defense.officers_at(source,source_king,sr,sg::general_count)>=2 &&
                   ai_city_defense.officers_at(target,target_king,tr,sg::general_count)>=2) {
                    attack_city=source; target_city=target; attacker_king=source_king; defender_king=target_king; break;
                }
            }
        }
    }
    assert(attack_city>=0 && target_city>=0);
    source_count=ai_city_defense.officers_at(attack_city,attacker_king,source_roster,sg::general_count);
    target_count=ai_city_defense.officers_at(target_city,defender_king,target_roster,sg::general_count);
    const int strong_attacker=source_roster[0],weak_attacker=source_roster[1];
    const int captured_then_released=target_roster[0],strong_defender=target_roster[1];
    for(int i=2;i<target_count;++i) {
        auto& extra=ai_city_defense.campaign.officers[target_roster[i]];
        extra.status=sg::Status::absent; extra.city=-1;
    }
    auto& strong_attack_officer=ai_city_defense.campaign.officers[strong_attacker];
    strong_attack_officer.rank=49; strong_attack_officer.front=uint16_t(ai_city_defense.front_capacity(strong_attacker));
    strong_attack_officer.soldiers=uint16_t(ai_city_defense.max_soldiers(strong_attacker));
    strong_attack_officer.hp=uint16_t(ai_city_defense.max_hp(strong_attacker));
    strong_attack_officer.mp=uint16_t(ai_city_defense.max_mp(strong_attacker));
    auto& weak_attack_officer=ai_city_defense.campaign.officers[weak_attacker];
    weak_attack_officer.rank=1; weak_attack_officer.front=1; weak_attack_officer.soldiers=1;
    weak_attack_officer.hp=1; weak_attack_officer.mp=0;
    auto& weak_defense_officer=ai_city_defense.campaign.officers[captured_then_released];
    weak_defense_officer.rank=1; weak_defense_officer.front=1; weak_defense_officer.soldiers=1;
    weak_defense_officer.hp=1; weak_defense_officer.mp=0;
    sg::set_native_escape_count(ai_city_defense.campaign,captured_then_released,6);
    auto& strong_defense_officer=ai_city_defense.campaign.officers[strong_defender];
    strong_defense_officer.rank=49; strong_defense_officer.front=uint16_t(ai_city_defense.front_capacity(strong_defender));
    strong_defense_officer.soldiers=uint16_t(ai_city_defense.max_soldiers(strong_defender));
    strong_defense_officer.hp=uint16_t(ai_city_defense.max_hp(strong_defender));
    strong_defense_officer.mp=uint16_t(ai_city_defense.max_mp(strong_defender));
    strong_attack_officer.city=weak_attack_officer.city=-1;
    strong_attack_officer.army=weak_attack_officer.army=0;
    auto& defense_army=ai_city_defense.campaign.armies[0];
    defense_army={}; defense_army.active=1; defense_army.count=2; defense_army.king=int8_t(attacker_king);
    defense_army.origin=int8_t(attack_city); defense_army.target=int8_t(target_city); defense_army.total=10;
    defense_army.generals[0]=int16_t(strong_attacker); defense_army.generals[1]=int16_t(weak_attacker);
    defense_army.retreat_goal=-1;
    ai_city_defense.campaign.phase=sg::Phase::world;
    ai_city_defense.start_battle(0);
    assert(ai_city_defense.battle.winner==1);
    assert(ai_city_defense.campaign.cities[target_city].king==defender_king);
    assert(weak_defense_officer.status==sg::Status::employed && weak_defense_officer.king==defender_king);
    assert(weak_defense_officer.city==target_city && weak_defense_officer.army==-1);
    assert(ai_city_defense.valid());

    // AI field battles use the same demake general matchups, then settle the
    // army records: captured generals and carried prisoners join the winner.
    sg::Game ai_field_battle;
    ai_field_battle.new_game(1,1);
    attack_city=-1; target_city=-1; attacker_king=-1; defender_king=-1;
    for(int source=0;source<sg::city_count && attack_city<0;++source) {
        const int source_king=ai_field_battle.campaign.cities[source].king;
        if(source_king<0 || source_king==ai_field_battle.campaign.player) continue;
        for(int n=0;n<sg::city_neighbor_counts[source];++n) {
            const int target=sg::city_neighbor_order[source][n];
            const int target_king=ai_field_battle.campaign.cities[target].king;
            if(target_king>=0 && target_king!=ai_field_battle.campaign.player && target_king!=source_king) {
                attack_city=source; target_city=target; attacker_king=source_king; defender_king=target_king; break;
            }
        }
    }
    assert(attack_city>=0 && target_city>=0);
    source_count=ai_field_battle.officers_at(attack_city,attacker_king,source_roster,sg::general_count);
    target_count=ai_field_battle.officers_at(target_city,defender_king,target_roster,sg::general_count);
    assert(source_count>0 && target_count>0);
    const int field_winner=source_roster[0],field_loser=target_roster[0];
    auto& field_winning_officer=ai_field_battle.campaign.officers[field_winner];
    field_winning_officer.rank=49; field_winning_officer.front=uint16_t(ai_field_battle.front_capacity(field_winner));
    field_winning_officer.soldiers=uint16_t(ai_field_battle.max_soldiers(field_winner));
    field_winning_officer.hp=uint16_t(ai_field_battle.max_hp(field_winner));
    field_winning_officer.mp=uint16_t(ai_field_battle.max_mp(field_winner));
    auto& field_losing_officer=ai_field_battle.campaign.officers[field_loser];
    field_losing_officer.rank=1; field_losing_officer.front=1; field_losing_officer.soldiers=1;
    field_losing_officer.hp=1; field_losing_officer.mp=0;
    sg::set_native_escape_count(ai_field_battle.campaign,field_loser,6);
    int field_cargo=-1;
    for(int g=0;g<sg::general_count;++g) {
        auto& officer=ai_field_battle.campaign.officers[g];
        if(officer.status==sg::Status::employed && officer.king>=0 &&
           officer.king!=attacker_king && officer.king!=defender_king) {
            field_cargo=g; officer.status=sg::Status::prisoner; officer.king=int8_t(defender_king);
            officer.city=-1; officer.army=1; break;
        }
    }
    assert(field_cargo>=0);
    field_winning_officer.city=field_losing_officer.city=-1;
    field_winning_officer.army=0; field_losing_officer.army=1;
    auto& field_a=ai_field_battle.campaign.armies[0];
    field_a={}; field_a.active=1; field_a.count=1; field_a.king=int8_t(attacker_king);
    field_a.origin=int8_t(attack_city); field_a.target=int8_t(target_city); field_a.total=20;
    field_a.remaining=10; field_a.generals[0]=int16_t(field_winner); field_a.retreat_goal=-1;
    auto& field_b=ai_field_battle.campaign.armies[1];
    field_b={}; field_b.active=1; field_b.count=1; field_b.king=int8_t(defender_king);
    field_b.origin=int8_t(target_city); field_b.target=int8_t(attack_city); field_b.total=20;
    field_b.remaining=10; field_b.money=99; field_b.generals[0]=int16_t(field_loser);
    field_b.retreat_goal=-1;
    ai_field_battle.campaign.phase=sg::Phase::world;
    ai_field_battle.start_field_battle(0,1);
    assert(ai_field_battle.battle.winner==0);
    assert(ai_field_battle.campaign.phase==sg::Phase::world);
    assert(field_a.active && field_a.count==1 && field_a.money==99);
    assert(!field_b.active);
    assert(field_losing_officer.status==sg::Status::prisoner && field_losing_officer.king==attacker_king);
    assert(field_losing_officer.army==0 && field_losing_officer.city==-1);
    assert(ai_field_battle.campaign.officers[field_cargo].king==attacker_king);
    assert(ai_field_battle.campaign.officers[field_cargo].army==0);
    assert(ai_field_battle.valid());

    // A faction ruler with cities always escapes this field pairing under
    // demake's capture threshold, so the defeated army must retreat intact.
    sg::Game ai_field_retreat;
    ai_field_retreat.new_game(1,1);
    attack_city=-1; target_city=-1; attacker_king=-1; defender_king=-1;
    for(int source=0;source<sg::city_count && attack_city<0;++source) {
        const int source_king=ai_field_retreat.campaign.cities[source].king;
        if(source_king<0 || source_king==ai_field_retreat.campaign.player) continue;
        for(int n=0;n<sg::city_neighbor_counts[source];++n) {
            const int target=sg::city_neighbor_order[source][n];
            const int target_king=ai_field_retreat.campaign.cities[target].king;
            if(target_king>=0 && target_king!=ai_field_retreat.campaign.player && target_king!=source_king) {
                attack_city=source; target_city=target; attacker_king=source_king; defender_king=target_king; break;
            }
        }
    }
    assert(attack_city>=0 && target_city>=0);
    const int retreat_ruler=sg::king_defs[ai_field_retreat.campaign.scenario][attacker_king].general;
    source_count=ai_field_retreat.officers_at(target_city,defender_king,source_roster,sg::general_count);
    assert(source_count>0);
    const int retreat_winner=source_roster[0];
    auto& ruler_officer=ai_field_retreat.campaign.officers[retreat_ruler];
    ruler_officer.city=-1; ruler_officer.army=0; ruler_officer.rank=1;
    ruler_officer.front=1; ruler_officer.soldiers=1; ruler_officer.hp=1; ruler_officer.mp=0;
    auto& retreat_winner_officer=ai_field_retreat.campaign.officers[retreat_winner];
    retreat_winner_officer.city=-1; retreat_winner_officer.army=1;
    retreat_winner_officer.rank=49;
    retreat_winner_officer.front=uint16_t(ai_field_retreat.front_capacity(retreat_winner));
    retreat_winner_officer.soldiers=uint16_t(ai_field_retreat.max_soldiers(retreat_winner));
    retreat_winner_officer.hp=uint16_t(ai_field_retreat.max_hp(retreat_winner));
    retreat_winner_officer.mp=uint16_t(ai_field_retreat.max_mp(retreat_winner));
    auto& retreat_losing_army=ai_field_retreat.campaign.armies[0];
    retreat_losing_army={}; retreat_losing_army.active=1; retreat_losing_army.count=1;
    retreat_losing_army.king=int8_t(attacker_king); retreat_losing_army.origin=int8_t(attack_city);
    retreat_losing_army.target=int8_t(target_city); retreat_losing_army.total=20;
    retreat_losing_army.remaining=10; retreat_losing_army.retreat_goal=-1;
    retreat_losing_army.generals[0]=int16_t(retreat_ruler);
    auto& retreat_winning_army=ai_field_retreat.campaign.armies[1];
    retreat_winning_army={}; retreat_winning_army.active=1; retreat_winning_army.count=1;
    retreat_winning_army.king=int8_t(defender_king); retreat_winning_army.origin=int8_t(target_city);
    retreat_winning_army.target=int8_t(attack_city); retreat_winning_army.total=20;
    retreat_winning_army.remaining=10; retreat_winning_army.retreat_goal=-1;
    retreat_winning_army.generals[0]=int16_t(retreat_winner);
    ai_field_retreat.campaign.phase=sg::Phase::world;
    ai_field_retreat.start_field_battle(0,1);
    assert(ai_field_retreat.battle.winner==1);
    assert(retreat_losing_army.active && retreat_losing_army.count==1);
    assert(retreat_losing_army.retreating==2 && retreat_losing_army.retreat_goal==attack_city);
    assert(ruler_officer.status==sg::Status::employed && ruler_officer.army==0);
    assert(ai_field_retreat.valid());

    // demake runs AI domestic actions after the player's internal affairs,
    // not at rollover. A same-faction prisoner surrenders only on that pass.
    game.new_game(1,1);
    game.campaign.phase=sg::Phase::world;
    int captive_city=-1,captive_king=-1,loyal_prisoner=-1;
    for(int city=0;city<sg::city_count;++city) {
        const int king=game.campaign.cities[city].king;
        if(king<0 || king==game.campaign.player) continue;
        captive_city=city;
        captive_king=king;
        break;
    }
    assert(captive_city>=0);
    for(int g=0;g<sg::general_count;++g) {
        if(g==game.campaign.officers[g].king) continue;
        loyal_prisoner=g;
        break;
    }
    assert(loyal_prisoner>=0);
    auto& captive=game.campaign.officers[loyal_prisoner];
    captive.status=sg::Status::prisoner;
    captive.king=int8_t(captive_king);
    captive.city=int16_t(captive_city);
    captive.army=-1;
    captive.loyalty=100;
    sg::native_origin_king(game.campaign,loyal_prisoner)=uint8_t(captive_king);
    game.campaign.month=12;
    game.campaign.month_ticks=299;
    game.world_tick();
    assert(game.campaign.phase==sg::Phase::domestic);
    assert(captive.status==sg::Status::prisoner);
    game.finish_domestic();
    assert(game.campaign.phase==sg::Phase::world);
    assert(captive.status==sg::Status::employed);
    assert(captive.king==captive_king && captive.city==captive_city && captive.loyalty==90);
    stable();

    // demake's domestic prisoner flow uses loyalty for the persuasion roll,
    // lowers loyalty after a refusal, and fully restores a newly recruited
    // general. Both city and post-battle entry points use these rules.
    game.new_game(1,1);
    int persuade_general=0;
    while(sg::general_initial[1][persuade_general].loyal_king>=0) ++persuade_general;
    const int persuade_city=5;
    auto& persuade_captive=game.campaign.officers[persuade_general];
    persuade_captive.status=sg::Status::prisoner;
    persuade_captive.king=int8_t(game.campaign.player);
    persuade_captive.city=int16_t(persuade_city);
    persuade_captive.army=-1;
    persuade_captive.loyalty=100;
    persuade_captive.soldiers=0;
    persuade_captive.front=0;
    sg::native_origin_king(game.campaign,persuade_general)=0;
    sg::Game expected_persuasion=game;
    expected_persuasion.roll(100); // At 100 loyalty, success chance is zero.
    const int expected_loyalty=100-5-expected_persuasion.roll(15);
    assert(!game.persuade_prisoner(persuade_general));
    assert(persuade_captive.status==sg::Status::prisoner && persuade_captive.king==game.campaign.player);
    assert(persuade_captive.loyalty==expected_loyalty && persuade_captive.city==persuade_city);

    // A former member of the player's own faction is automatically returned,
    // exactly as demake's `gInfo.king == Controller.kingIndex` branch.
    persuade_captive.loyalty=20;
    sg::native_origin_king(game.campaign,persuade_general)=uint8_t(game.campaign.player);
    const uint32_t persuasion_rng=game.campaign.random;
    const uint32_t persuasion_rng_secondary=game.campaign.random_secondary;
    assert(game.persuade_prisoner(persuade_general));
    assert(persuade_captive.status==sg::Status::employed && persuade_captive.king==game.campaign.player);
    assert(persuade_captive.loyalty==90 && persuade_captive.city==persuade_city && persuade_captive.army==-1);
    assert(persuade_captive.soldiers==game.max_soldiers(persuade_general));
    assert(persuade_captive.front==game.front_capacity(persuade_general));
    assert(game.campaign.random==persuasion_rng && game.campaign.random_secondary==persuasion_rng_secondary);

    persuade_captive.status=sg::Status::prisoner;
    persuade_captive.loyalty=100;
    sg::native_origin_king(game.campaign,persuade_general)=0;
    game.handle_prisoner(persuade_general,true);
    assert(persuade_captive.status==sg::Status::prisoner && persuade_captive.loyalty<100);
    sg::native_origin_king(game.campaign,persuade_general)=uint8_t(game.campaign.player);
    game.handle_prisoner(persuade_general,true);
    assert(persuade_captive.status==sg::Status::employed && persuade_captive.loyalty==90);

    // A captive on the road cannot join via the battle-result shortcut; a
    // release removes the transport link and places them at the nearer city.
    persuade_captive.status=sg::Status::prisoner;
    persuade_captive.city=-1;
    persuade_captive.army=0;
    auto& prisoner_transport=game.campaign.armies[0];
    prisoner_transport.active=1;
    prisoner_transport.origin=5;
    prisoner_transport.target=6;
    prisoner_transport.total=100;
    prisoner_transport.remaining=25;
    const uint32_t transport_rng=game.campaign.random;
    const uint32_t transport_secondary=game.campaign.random_secondary;
    game.handle_prisoner(persuade_general,true);
    assert(persuade_captive.status==sg::Status::prisoner && persuade_captive.army==0);
    assert(game.campaign.random==transport_rng && game.campaign.random_secondary==transport_secondary);
    game.handle_prisoner(persuade_general,false);
    assert(persuade_captive.status==sg::Status::wild && persuade_captive.king==-1);
    assert(persuade_captive.army==-1 && persuade_captive.city==6);
    assert(!sg::native_status2_countdown(game.campaign,persuade_general));

    // demake enables prefect appointment whenever the faction ruler is away,
    // even with one stationed general, and allows replacing the ruler's old
    // prefect assignment. There is no two-general minimum in that UI flow.
    game.new_game(1,1);
    constexpr int prefect_city=5, single_prefect=0;
    const int prefect_ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
    for(int g=0;g<sg::general_count;++g) {
        auto& officer=game.campaign.officers[g];
        if(g!=single_prefect && officer.king==game.campaign.player && officer.city==prefect_city) {
            officer.status=sg::Status::absent;
            officer.city=-1;
            officer.army=-1;
        }
    }
    auto& only_prefect=game.campaign.officers[single_prefect];
    only_prefect.status=sg::Status::employed;
    only_prefect.king=int8_t(game.campaign.player);
    only_prefect.city=prefect_city;
    only_prefect.army=-1;
    game.campaign.officers[prefect_ruler].city=-1;
    game.campaign.cities[prefect_city].major=int16_t(prefect_ruler);
    assert(game.officers_at(prefect_city,game.campaign.player,nullptr,0)==0);
    int one_general_roster[1];
    assert(game.officers_at(prefect_city,game.campaign.player,one_general_roster,1)==1);
    assert(one_general_roster[0]==single_prefect);
    assert(game.appoint_major(prefect_city,single_prefect));
    assert(game.campaign.cities[prefect_city].major==single_prefect);

    // demake's conscription slider operates on total troops but fills the
    // cavalry pool before infantry, while reducing infantry before cavalry.
    game.new_game(1,1);
    game.campaign.phase = sg::Phase::domestic;
    int transfer_city=-1, transfer_general=-1;
    const int transfer_ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
    for(int c=0;c<sg::city_count && transfer_general<0;++c) {
        if(game.campaign.cities[c].king!=game.campaign.player ||
           sg::original::demake_reserve_capacity(c)<100) continue;
        int roster[sg::general_count];
        const int count=game.officers_at(c,game.campaign.player,roster,sg::general_count);
        for(int i=0;i<count;++i) if(roster[i]!=transfer_ruler) {
            transfer_city=c; transfer_general=roster[i]; break;
        }
    }
    assert(transfer_city>=0 && transfer_general>=0);
    auto& transfer_officer=game.campaign.officers[transfer_general];
    auto& transfer_c=game.campaign.cities[transfer_city];
    transfer_officer.rank=30;
    transfer_officer.soldiers=0;
    transfer_officer.front=0;
    transfer_officer.hp=uint16_t(game.max_hp(transfer_general));
    transfer_officer.mp=uint16_t(game.max_mp(transfer_general));
    transfer_c.reserve=100;
    assert(game.rear_capacity(transfer_general)==50 && game.front_capacity(transfer_general)==50);
    assert(game.transfer_soldiers(transfer_city,transfer_general,10));
    assert(transfer_officer.soldiers==10 && transfer_officer.front==0 && transfer_c.reserve==90);
    assert(game.transfer_soldiers(transfer_city,transfer_general,45));
    assert(transfer_officer.soldiers==55 && transfer_officer.front==5 && transfer_c.reserve==45);
    assert(game.transfer_soldiers(transfer_city,transfer_general,1000));
    assert(transfer_officer.soldiers==100 && transfer_officer.front==50 && transfer_c.reserve==0);
    assert(!game.transfer_soldiers(transfer_city,transfer_general,1));
    assert(game.transfer_soldiers(transfer_city,transfer_general,-7));
    assert(transfer_officer.soldiers==93 && transfer_officer.front==43 && transfer_c.reserve==7);
    stable();

    // demake MonthAct restores STR/20 HP and INT/20 MP each ordinary month;
    // daily world ticks do not recover officers, and the year rollover skips it.
    game.new_game(1,1);
    game.campaign.phase = sg::Phase::world;
    int recovery_general = -1;
    for(int i = 0; i < sg::general_count; ++i) {
        const auto& o = game.campaign.officers[i];
        if(o.status == sg::Status::employed && o.city >= 0 && o.army < 0) {
            recovery_general = i;
            break;
        }
    }
    assert(recovery_general >= 0);
    auto& recovering = game.campaign.officers[recovery_general];
    recovering.hp = uint16_t(sg::maximum(1,game.max_hp(recovery_general)-10));
    recovering.mp = uint16_t(sg::maximum(1,game.max_mp(recovery_general)-10));
    const int monthly_hp = game.strength(recovery_general)/20;
    const int monthly_mp = game.intelligence(recovery_general)/20;
    const int hp_before_month = recovering.hp;
    const int mp_before_month = recovering.mp;
    game.campaign.month_ticks = 299;
    game.world_tick();
    assert(recovering.hp == sg::minimum(game.max_hp(recovery_general),hp_before_month+monthly_hp));
    assert(recovering.mp == sg::minimum(game.max_mp(recovery_general),mp_before_month+monthly_mp));
    const int hp_before_year = recovering.hp;
    const int mp_before_year = recovering.mp;
    game.campaign.month = 12;
    game.campaign.month_ticks = 299;
    game.world_tick();
    assert(game.campaign.phase == sg::Phase::domestic);
    assert(recovering.hp == hp_before_year && recovering.mp == mp_before_year);
    game.campaign.phase = sg::Phase::world;
    game.campaign.month_ticks = 0;
    int front_limit = game.front_capacity(recovery_general);
    int rear_limit = game.rear_capacity(recovery_general);
    recovering.hp = 0;
    recovering.mp = 0;
    recovering.front = uint16_t(front_limit+1);
    recovering.soldiers = uint16_t(front_limit+rear_limit+20);
    game.world_tick();
    assert(recovering.hp == 1 && recovering.mp == 1);
    assert(recovering.front == front_limit);
    assert(recovering.soldiers == front_limit+rear_limit);
    stable();

    // demake AddReservist runs every 30 GBA world ticks, only for AI cities,
    // and spends exactly 100 money per reserve up to the fixed city cap.
    game.new_game(1,1);
    game.campaign.phase = sg::Phase::world;
    int ai_city = -1;
    for(int city = 0; city < sg::city_count; ++city)
        if(game.campaign.cities[city].king >= 0 && game.campaign.cities[city].king != game.campaign.player &&
           game.campaign.cities[city].money > 100) { ai_city = city; break; }
    assert(ai_city >= 0);
    auto& ai_reserve_city = game.campaign.cities[ai_city];
    ai_reserve_city.reserve = uint16_t(sg::original::demake_reserve_capacity(ai_city)-1);
    ai_reserve_city.reserve_ticks = 1;
    const uint32_t ai_money_before = ai_reserve_city.money;
    game.world_tick();
    assert(ai_reserve_city.reserve == sg::original::demake_reserve_capacity(ai_city));
    assert(ai_reserve_city.money == ai_money_before-100);
    assert(ai_reserve_city.reserve_ticks == sg::original::demake_reservist_interval);
    int player_city = -1;
    for(int city = 0; city < sg::city_count; ++city)
        if(game.campaign.cities[city].king == game.campaign.player) { player_city = city; break; }
    assert(player_city >= 0);
    auto& player_reserve_city = game.campaign.cities[player_city];
    player_reserve_city.reserve=0;
    player_reserve_city.reserve_ticks = 1;
    player_reserve_city.money = 1000;
    const uint32_t player_money_before = player_reserve_city.money;
    game.world_tick();
    assert(player_reserve_city.reserve == 1 && player_reserve_city.money == player_money_before-100);
    player_reserve_city.money=100; player_reserve_city.reserve_ticks=1;
    game.world_tick();
    assert(player_reserve_city.reserve==1 && player_reserve_city.money==100);
    player_reserve_city.money=1000;
    player_reserve_city.reserve=uint16_t(sg::original::demake_reserve_capacity(player_city));
    player_reserve_city.reserve_ticks=1;
    game.world_tick();
    assert(player_reserve_city.reserve==sg::original::demake_reserve_capacity(player_city) && player_reserve_city.money==1000);
    stable();

    // demake CityAct refills stationed AI soldiers monthly, consuming reserve
    // in roster order; a full earlier officer does not consume any reserve.
    game.new_game(1,1);
    game.campaign.phase = sg::Phase::world;
    int reinforcement_city = -1, reinforcement_general = -1;
    int reinforcement_roster[sg::general_count];
    for(int city = 0; city < sg::city_count && reinforcement_city < 0; ++city) {
        const int king = game.campaign.cities[city].king;
        if(king < 0 || king == game.campaign.player) continue;
        const int count = game.officers_at(city,king,reinforcement_roster,sg::general_count);
        if(count) { reinforcement_city = city; reinforcement_general = reinforcement_roster[count-1]; }
    }
    assert(reinforcement_city >= 0 && reinforcement_general >= 0);
    auto& reinforcement = game.campaign.cities[reinforcement_city];
    const int reinforcement_king = reinforcement.king;
    const int reinforcement_count = game.officers_at(reinforcement_city,reinforcement_king,reinforcement_roster,sg::general_count);
    for(int n = 0; n < reinforcement_count; ++n)
        game.campaign.officers[reinforcement_roster[n]].soldiers = uint16_t(game.max_soldiers(reinforcement_roster[n]));
    game.campaign.officers[reinforcement_general].soldiers = 0;
    game.campaign.officers[reinforcement_general].front = 0;
    reinforcement.reserve = 20;
    reinforcement.money = 100;
    game.campaign.month_ticks = 299;
    game.world_tick();
    assert(game.campaign.officers[reinforcement_general].soldiers == game.max_soldiers(reinforcement_general));
    assert(reinforcement.reserve == 10);
    const int expected_added=sg::minimum(20,game.max_soldiers(reinforcement_general));
    const int expected_cavalry=sg::minimum(expected_added,game.rear_capacity(reinforcement_general));
    assert(game.campaign.officers[reinforcement_general].front == expected_added-expected_cavalry);
    stable();

    // demake IntoTheCity only admits enough army generals to reach a ten-man
    // city roster; a remainder stays garrisoned with its money and prisoners.
    game.new_game(0,0);
    game.campaign.phase=sg::Phase::world;
    const int partial_origin=0;
    int partial_neighbors[sg::city_count];
    const int partial_neighbor_count=game.neighbors(partial_origin,partial_neighbors);
    assert(partial_neighbor_count>0);
    const int partial_city=partial_neighbors[0];
    for(auto& city:game.campaign.cities) city.king=-1;
    game.campaign.cities[partial_origin].king=1;
    game.campaign.cities[partial_city].king=1;
    game.campaign.cities[partial_city].money=321;
    game.campaign.cities[partial_city].major=0;
    for(int g=0;g<sg::general_count;++g) {
        auto& officer=game.campaign.officers[g];
        officer.status=sg::Status::absent; officer.king=-1; officer.city=-1; officer.army=-1;
    }
    for(int g=0;g<12;++g) {
        auto& officer=game.campaign.officers[g]; officer.status=sg::Status::employed;
        officer.king=1; officer.city=int16_t(g<9 ? partial_city : -1);
        officer.army=int16_t(g<9 ? -1 : 0);
    }
    // demake admits the roster prefix, not the tail: commander 9 enters first.
    auto& entered_general=game.campaign.officers[9];
    sg::set_native_escape_count(game.campaign,9,3);
    auto& partial_captive=game.campaign.officers[12];
    partial_captive.status=sg::Status::prisoner; partial_captive.king=1;
    partial_captive.city=-1; partial_captive.army=0;
    auto& partial_army=game.campaign.armies[0];
    partial_army={}; partial_army.active=1; partial_army.count=3; partial_army.king=1;
    partial_army.origin=int8_t(partial_origin); partial_army.target=int8_t(partial_city);
    partial_army.total=10; partial_army.remaining=0; partial_army.money=77;
    partial_army.retreat_goal=-1;
    partial_army.generals[0]=9; partial_army.generals[1]=10; partial_army.generals[2]=11;
    auto moving_entry=game;
    moving_entry.campaign.player=1;
    moving_entry.campaign.armies[0].remaining=moving_entry.campaign.armies[0].total;
    const uint32_t origin_money=moving_entry.campaign.cities[partial_origin].money;
    assert(moving_entry.enter_army_city(0));
    assert(!moving_entry.campaign.armies[0].active);
    for(int g=9;g<=11;++g)
        assert(moving_entry.campaign.officers[g].city==partial_origin &&
               moving_entry.campaign.officers[g].army==-1);
    assert(moving_entry.campaign.officers[12].city==partial_origin &&
           moving_entry.campaign.officers[12].army==-1);
    assert(moving_entry.campaign.cities[partial_origin].money==origin_money+77);
    assert(moving_entry.valid());
    game.world_tick();
    int partial_roster[sg::general_count];
    assert(game.officers_at(partial_city,1,partial_roster,sg::general_count)==10);
    assert(entered_general.city==partial_city && entered_general.army==-1);
    assert(sg::native_escape_count(game.campaign,9)==0);
    assert(partial_army.active && partial_army.count==2 && partial_army.money==77);
    assert(partial_army.garrisoned && partial_army.origin==partial_city && partial_army.target==partial_city);
    assert(partial_army.total==0 && partial_army.remaining==0);
    assert(partial_army.generals[0]==10 && partial_army.generals[1]==11);
    assert(partial_captive.status==sg::Status::prisoner && partial_captive.army==0 && partial_captive.city==-1);
    assert(game.campaign.cities[partial_city].money==321);
    assert(game.valid());
    auto full_city_entry=game;
    full_city_entry.campaign.player=1;
    auto& full_city_army=full_city_entry.campaign.armies[0];
    full_city_army.garrisoned=1;
    assert(full_city_entry.enter_army_city(0)); // demake keeps the command enabled when capacity is zero.
    assert(full_city_army.active && full_city_army.garrisoned && full_city_army.count==2);
    assert(full_city_army.money==77 && full_city_army.generals[0]==10 && full_city_army.generals[1]==11);
    assert(full_city_entry.campaign.cities[partial_city].money==321);
    assert(full_city_entry.campaign.officers[12].status==sg::Status::prisoner &&
           full_city_entry.campaign.officers[12].army==0 && full_city_entry.campaign.officers[12].city==-1);
    assert(full_city_entry.valid());
    // Natural arrival at a full city must also settle; it cannot remain a
    // zero-distance moving army indefinitely waiting for a manual command.
    auto full_arrival=game;
    full_arrival.campaign.phase=sg::Phase::world;
    full_arrival.campaign.player=1;
    auto& waiting=full_arrival.campaign.armies[0];
    waiting.garrisoned=0; waiting.origin=int8_t(partial_origin);
    waiting.total=10; waiting.remaining=0;
    full_arrival.world_tick();
    assert(waiting.active && waiting.garrisoned && waiting.count==2);
    assert(waiting.origin==partial_city && waiting.target==partial_city && waiting.total==0);
    assert(full_arrival.valid());
    stable();

    // When demake AI sends the prefect, the source city appoints its ruler if
    // present, otherwise the strongest general still stationed there.
    game.new_game(0,0);
    game.campaign.phase=sg::Phase::world;
    int prefect_neighbors[sg::city_count];
    const int prefect_neighbor_count=game.neighbors(0,prefect_neighbors);
    assert(prefect_neighbor_count>0);
    for(auto& city:game.campaign.cities) city.king=-1;
    game.campaign.cities[0].king=1; game.campaign.cities[0].money=100;
    const int city_ruler=sg::king_defs[game.campaign.scenario][1].general;
    for(auto& officer:game.campaign.officers) {
        officer.status=sg::Status::absent; officer.king=-1; officer.city=-1; officer.army=-1;
    }
    int prefect_fixture[3],prefect_fixture_count=0;
    for(int g=0;g<sg::general_count && prefect_fixture_count<3;++g) if(g!=city_ruler) {
        prefect_fixture[prefect_fixture_count++]=g;
        auto& officer=game.campaign.officers[g]; officer.status=sg::Status::employed;
        officer.king=1; officer.city=0;
    }
    assert(prefect_fixture_count==3);
    game.campaign.cities[0].major=int16_t(prefect_fixture[0]);
    const int prefect_dispatch[1]={prefect_fixture[0]};
    assert(game.send_army(0,prefect_neighbors[0],prefect_dispatch,1,false,true));
    int expected_prefect=prefect_fixture[1];
    for(int i=1;i<3;++i)
        if(game.strength(prefect_fixture[i])>game.strength(expected_prefect)) expected_prefect=prefect_fixture[i];
    assert(game.campaign.cities[0].major==expected_prefect);
    assert(game.valid());
    stable();

    // demake AI armies carry the source city's prisoners on a >60 roll and
    // deposit them at the destination city after a friendly arrival.
    bool transported_prisoner_seen = false;
    const int prisoner_origin = 0;
    int prisoner_route[sg::city_count];
    const int prisoner_route_count = game.neighbors(prisoner_origin,prisoner_route);
    assert(prisoner_route_count > 0);
    for(uint32_t seed = 1; seed <= 128 && !transported_prisoner_seen; ++seed) {
        game.new_game(0,0);
        game.campaign.phase = sg::Phase::world;
        for(auto& city : game.campaign.cities) city.king = -1;
        const int prisoner_destination = prisoner_route[0];
        game.campaign.cities[prisoner_origin].king = 1;
        game.campaign.cities[prisoner_origin].major = -1;
        game.campaign.cities[prisoner_origin].money = 1000;
        game.campaign.cities[prisoner_destination].king = 1;
        int owned = 1;
        while(owned == prisoner_origin || owned == prisoner_destination) ++owned;
        game.campaign.cities[owned].king = 0;
        for(auto& officer : game.campaign.officers) {
            officer.status = sg::Status::absent;
            officer.king = -1;
            officer.city = -1;
            officer.army = -1;
        }
        auto& escort = game.campaign.officers[0];
        escort.status = sg::Status::employed;
        escort.king = 1;
        escort.city = int16_t(prisoner_origin);
        escort.hp = 80;
        escort.soldiers = uint16_t(game.max_soldiers(0));
        escort.front = uint16_t(game.front_capacity(0));
        constexpr int prisoner_general = 1;
        auto& captive = game.campaign.officers[prisoner_general];
        captive.status = sg::Status::prisoner;
        captive.king = 1;
        captive.city = int16_t(prisoner_origin);
        game.campaign.random = seed*2654435761u;
        game.campaign.random_secondary = seed*2246822519u+3u;
        const int escort_roster[1] = {0};
        if(!game.send_army(prisoner_origin,prisoner_destination,escort_roster,1,false,true)) continue;
        const int prisoner_army = escort.army;
        if(captive.army != prisoner_army || captive.city != -1) continue;
        game.campaign.armies[prisoner_army].remaining = 0;
        game.world_tick();
        assert(!game.campaign.armies[prisoner_army].active);
        assert(captive.status == sg::Status::prisoner && captive.city == prisoner_destination && captive.army == -1);
        transported_prisoner_seen = true;
    }
    assert(transported_prisoner_seen);
    stable();

    // demake AI attacks are evaluated by monthly CityAct, not by the old
    // native 120-tick dispatcher. Sweep RNG seeds to cover its 19% neutral-city
    // attack branch and assert no army appears before the month boundary.
    int attack_neighbors[sg::city_count];
    const int attack_neighbor_count = game.neighbors(0,attack_neighbors);
    assert(attack_neighbor_count > 0);
    game.new_game(0,0);
    game.campaign.phase = sg::Phase::world;
    game.campaign.random = 4;
    game.campaign.random_secondary = 71;
    for(auto& city : game.campaign.cities) city.king = -1;
    game.campaign.cities[0].king = 1;
    game.campaign.cities[0].major = -1;
    game.campaign.cities[0].money = 1000;
    int owned_anchor = 1;
    while(owned_anchor == attack_neighbors[0]) ++owned_anchor;
    game.campaign.cities[owned_anchor].king = 0;
    for(auto& officer : game.campaign.officers) {
        officer.status = sg::Status::absent;
        officer.king = -1;
        officer.city = -1;
        officer.army = -1;
    }
    for(int g = 0; g < 4; ++g) {
        auto& officer = game.campaign.officers[g];
        officer.status = sg::Status::employed;
        officer.king = 1;
        officer.city = 0;
        officer.hp = 51;
        officer.soldiers = uint16_t(game.max_soldiers(g));
        officer.front = uint16_t(game.front_capacity(g));
    }
    game.campaign.cities[attack_neighbors[0]].king = -1;
    for(int tick = 0; tick < 120; ++tick) game.world_tick();
    bool dispatched_early = false;
    for(const auto& army : game.campaign.armies) dispatched_early |= army.active != 0;
    assert(!dispatched_early);
    game.campaign.month_ticks = 299;
    game.campaign.random = 4;
    game.campaign.random_secondary = 71; // First demake neutral-attack roll is 90.
    game.world_tick();
    bool demake_monthly_attack_seen = false;
    for(const auto& army : game.campaign.armies) demake_monthly_attack_seen |= army.active != 0;
    assert(demake_monthly_attack_seen);
    stable();

    uint32_t hash = game.checksum();
    ++game.campaign.cities[0].money;
    assert(game.checksum() != hash);
    game.campaign.officers[0].city = 1000;
    assert(!game.valid());
    printf("PASS: %d selectable campaigns, %d battles, reverse-road field encounter, recruited experience, weighted item search/use/equipment, annual income, demake monthly recovery, commands, ownership and invalid-state checks. Campaign RAM = %zu bytes\n",tested,battles,sizeof(sg::Campaign));
}

