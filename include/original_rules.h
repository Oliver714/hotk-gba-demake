#ifndef SG_ORIGINAL_RULES_H
#define SG_ORIGINAL_RULES_H
#include <stdint.h>
#include "data.h"

// Rules recovered from SANGO.exe SHA256 fc65b5de...b72f9c6 using REA/Ghidra.
// See analysis/native-rules.md and the complete REA Evidence JSON dossiers.
namespace sg::original {
constexpr uint32_t random_initial_primary = 0x12345678u;
constexpr uint32_t random_initial_secondary = 0x87654321u;
constexpr int maximum_level = 50;
constexpr int month_updates = 300;
constexpr int year_updates = 3600;
// Battle controller 0x443e5f initializes the native battle timer to 0x1734;
// 0x443b82 decrements it once per active battle update before resolving.
constexpr int battle_duel_timeout_seconds = 99;
constexpr int gba_battle_updates_per_second = 30;
// demake GeneralController: Escape uses runSpeed * 0.6. Normal GBA forward
// movement is 2 units / 3 updates, so retreat is 2 / 5. Spread single-unit
// steps over the five-update phase instead of storing fractional actor state.
constexpr int commander_retreat_step(uint32_t tick) {
    return tick%5==0 || tick%5==3 ? 1 : 0;
}
constexpr int battle_timeout_updates = battle_duel_timeout_seconds * gba_battle_updates_per_second;
// Native tactical decision timers start at 10 updates for the player side and
// 20 for the opposing side, then each reset to 60 after a decision.
constexpr int battle_ai_player_first_decision_updates = 10;
constexpr int battle_ai_opponent_first_decision_updates = 20;
constexpr int battle_ai_decision_interval_updates = 60;
// Native soldier/general action countdown resets to 0x20 updates normally.
// The movement helper sets actor flag 0x4000 and changes the timer to 0x25;
// the GBA maps this to modeled command8 movement (exact native flag coverage
// outside that path is not established).
constexpr int battle_action_interval = 0x20;
// demake GeneralController.OnDamage(type0) uses attacker minus defender STR.
// The receiver callback's left/right identities must not be reversed when
// evaluating the same check from the attacking side in the GBA renderer/model.
constexpr int commander_hit_threshold(int attacker_strength,int defender_strength) {
    const int difference=attacker_strength-defender_strength;
    return 60+(difference<-20 ? -20 : difference>20 ? 20 : difference);
}
constexpr int commander_attack_animation_updates = 21;
constexpr int commander_attack_cooldown_updates(int strength) {
    // demake: (1-STR/120f)/2 seconds AFTER the animation. At30Hz this is
    // ceil((120-STR)/8), clamped at0 for equipment-boosted STR>=120.
    return strength>=120 ? 0 : ((120-(strength<0 ? 0 : strength))+7)/8;
}
constexpr int commander_attack_cycle_updates(int strength) {
    return commander_attack_animation_updates+commander_attack_cooldown_updates(strength);
}
// GLOBAL 4700 waits 20+30+15 updates before starting its opposing-side lock
// script. Coordinate scans may add scheduler time beyond these explicit waits.
constexpr int magic_effect_launch_delay(int script) {
    // MAGIC01 3100 waits20+30, then60 more before selecting targets and
    // scheduling its arrow children. Their individual 81-110 waits follow.
    if(script >= 2600 && script <= 2605) return 110;
    // MAGIC01 3300/3400 waits 20 updates before launching its first child.
    if(script == 2608 || script == 2611) return 20;
    // GLOBAL3700 starts damaging 3705/3706 children after 20+30 setup,
    // visual placement, then the explicit 40-update wait.
    if(script == 2618 || script == 2622 || script == 2623) return 90;
    if(script == 2650) return 65;
    // GLOBAL 6260 (skill2665) starts its first collidable 2507 child after10.
    if(script == 2665) return 10;
    // GLOBAL 6000 (skills 2657-2659) waits 20+30+15 before laying its field.
    if(script >= 2657 && script <= 2659) return 65;
    // GLOBAL6400 waits20 updates before trying the requested placements.
    if(script == 2669 || script == 2670 || script == 2672 || script == 2673) return 15;
    // GLOBAL 2627/2628 start MAGIC01 4000, which waits 20 updates, then 30,
    // then 15 more before spawning its battlefield effects.
    if(script == 2627 || script == 2628) return 65;
    // MAGIC01 3900 waits 20 and 30 scheduler updates before target selection.
    if(script == 2624 || script == 2625) return 50;
    if(script >= 2639 && script <= 2641) return 96;
    return 24;
}
constexpr int magic_field_iteration_count(int script) {
    return script == 2657 ? 1 : script == 2658 ? 3 : script == 2659 ? 5 :
        script >= 2639 && script <= 2641 ? 13 : 0;
}
constexpr int chariot_visual_count(int script) {
    return script == 2627 ? 2 : script == 2628 ? 4 : 0;
}
constexpr int chariot_collision_object_count(int script) {
    return chariot_visual_count(script)*2; // visible M010 plus NULSHAPE proxy per lane.
}
constexpr int magic_field_objects_per_iteration(int script) {
    if(script == 2665 || (script >= 2639 && script <= 2641)) return 1;
    return magic_field_iteration_count(script) ? 13 : 0;
}
constexpr int magic_field_visual_objects_per_iteration(int script) {
    return script >= 2657 && script <= 2659 ? 22 : 0;
}
constexpr bool magic_uses_scripted_field(int script) {
    return magic_field_objects_per_iteration(script) != 0;
}
constexpr int magic_field_launch_delay(int script) {
    // GLOBAL4400 launches GLOBAL4420 after its 20+15 setup, the variant's
    // one-tick-per-child loop, then 32+64 synchronization waits.
    if(script == 2639) return 135;
    if(script == 2640) return 139;
    if(script == 2641) return 147;
    return magic_effect_launch_delay(script);
}
// Native casting immunity is enabled by each GLOBAL skill wrapper and cleared
// by a child GLOBAL2530 timer. Durations include the explicit waits before
// that child starts; reinforcement scripts also branch on their troop count.
constexpr int magic_immunity_duration(int script,int reinforcement_count=0) {
    if((script >= 2630 && script <= 2635) ||
       (script >= 2645 && script <= 2647) ||
       (script >= 2660 && script <= 2662)) return 36;
    if(script == 2626 || script == 2675) return 76;
    if(script == 2665) return 125;
    if(script >= 2636 && script <= 2638) return 78+18*(script-2636);
    if(script >= 2669 && script <= 2673) return 54;
    return 84;
}
// Native skill busy is cleared by native272 at the end of MAGIC01 4000.
// The 2627/2628 script reaches it after waits of 20+30+15+240 updates;
// its separate GLOBAL2530 child clears casting immunity after 20+64.
constexpr int magic_busy_duration(int script) {
    // GLOBAL 2600-2605 share MAGIC01 3100. The script's WAITs are
    // 20+30+60+60 updates before native272 clears the global cast lock.
    if(script >= 2600 && script <= 2605) return 170;
    // GLOBAL 2612-2614 share MAGIC01 3500: WAIT 20+34+10+30, then native272.
    if(script >= 2612 && script <= 2614) return 94;
    // MAGIC01 3600 mode2 runs 195 one-tick iterations, then waits for 3602:
    // 3601's RNG(10,50)+60+30, 3602's twenty RNG(0,5) waits+15. It then
    // waits through 3603's nine RNG(4,10) waits and a final WAIT15. The root
    // and child paths overlap; expected duration is about295 updates
    // (range266..380). cast_battle_skill replaces this fallback with the
    // sampled 3602/3603 completion schedule when it creates the effect tokens.
    if(script == 2617) return 295;
    // GLOBAL 6300 waits 20+15, runs its 60-update healing loop, then waits30.
    if(script == 2666 || script == 2667) return 125;
    // demake Tornado: .5s setup, .3s focus + .3s per cyclone, 1.5s tail.
    if(script >= 2636 && script <= 2638) return 78+18*(script-2636);
    // demake FireDragon: camera/setup .8s, sweep 2s, finish 1s (30Hz).
    if(script >= 2642 && script <= 2644) return 114;
    // MAGIC01 3300/3400 wait 20 updates before spawning, then 40 before
    // native272 clears the cast lock.
    if(script == 2608 || script == 2611) return 60;
    // MAGIC01 3900 waits20+30 before its strike setup, then60 before native272.
    if(script == 2624 || script == 2625) return 110;
    // GLOBAL3700 waits 20+30 before setup and 40+60 around volley creation.
    if(script == 2618 || script == 2622 || script == 2623) return 150;
    // GLOBAL4700 (skill2650) waits20+30 for setup, then154+15 before native272.
    if(script == 2650) return 219;
    if(script == 2663) return 330;
    if(script == 2627 || script == 2628) return 305;
    // GLOBAL 6200 (skill 2664) waits 20+30+60 updates before native272.
    if(script == 2664) return 110;
    if(script == 2665) return 60; // demake four-hit ShenGuiLuanWu cast gate.
    // GLOBAL 6000 waits 20+30+15, loops 1/3/5 times with RNG(1,8)
    // waits, then waits120 before native272. Return its maximum duration;
    // update_magic_field subtracts unused wait time at each original RNG draw.
    if(script == 2657) return 193;
    if(script == 2658) return 209;
    if(script == 2659) return 225;
    // MAGIC01 4400 reaches GLOBAL4420 after 20+15 setup updates, one launch
    // update per child (4/8/15), then32+64. The child barrier, eight-tick
    // wait, thirteen placement ticks, child wait30 and root wait30 extend it.
    if(script == 2639) return 165;
    if(script == 2640) return 169;
    if(script == 2641) return 176;
    return 84;
}
constexpr int magic_busy_duration(int script,int field_iterations) {
    // demake ShenGuiLuanWu runs four half-second strike visuals when the
    // commanders are close, and one non-damaging visual when they are not.
    // Keep the GBA cast gate finite while leaving enough time for all four
    // callback ticks to be observed by the battle loop.
    if(script == 2665) return field_iterations > 1 ? 60 : 30;
    if(script >= 2639 && script <= 2641) return magic_busy_duration(script) + 83;
    return magic_busy_duration(script);
}
// GLOBAL 4100 (2630-2632), 4200 (2660/2633-2635) and 4600 (2645-2647) poll their
// moving effect object every update, then wait 30 updates after it leaves the
// battlefield. GLOBAL4600's 2645-2647 object accelerates; the other listed
// paths use the constant 20/3 world-unit approximation.
constexpr int magic_busy_duration(int script,int field_iterations,
                                  int caster_x,int side,int battlefield_width,
                                  int reinforcement_count=0) {
    if(script == 2669 || script == 2670 || script == 2672 || script == 2673)
        // demake MagicAmbush: .5s rear focus, 1s presentation, .3s return.
        return 54;
    if(script >= 2645 && script <= 2647) {
        const int distance = side ? battlefield_width-caster_x : caster_x+1;
        int traveled=0, remainder=0, native_speed=1, acceleration=1, updates=0;
        while(traveled<distance && updates<1024) {
            const int total=remainder+native_speed*5;
            const int step=total/12;
            remainder=total-step*12;
            traveled+=step;
            if(native_speed<24) {
                native_speed+=acceleration;
                ++acceleration;
            }
            ++updates;
        }
        return 25+updates+30;
    }
    if((script >= 2630 && script <= 2632) || script == 2660 ||
       (script >= 2633 && script <= 2635)) {
        const int distance = side ? battlefield_width-caster_x : caster_x+1;
        const int travel_updates = (distance > 0 ? distance*3+19 : 0)/20;
        const int launch_updates = script >= 2630 && script <= 2632 ? 25 : 30;
        return launch_updates+travel_updates+30;
    }
    return magic_busy_duration(script,field_iterations);
}
// GLOBAL 2551 passes 20 to native 289; FUN_0043be22 adds that many 32-update
// action periods to the unit's deferred action delay.
constexpr int magic_action_delay(int script) { return script == 2650 ? 20 * battle_action_interval : 0; }
// FUN_004428e6 derives a battle grade from the selected officer's equipped
// item (+0x68) and learned skill slots (+0x42/+0x44/+0x46). These slots map to
// GBA learned[1..3] (skill tiers 10/20/40); the native caller clamps the skill
// component to 2 before adding the equipment component.
constexpr int battle_skill_grade(uint8_t tier2,uint8_t tier3,uint8_t tier4) {
    if(tier4 == 0x51) return 2; // Native return 3, then clamp to 2.
    if(tier4 == 0x29) return 2; // Native return 4, then clamp to 2.
    if(tier3) return 2;
    return tier2 ? 1 : 0;
}
constexpr int battle_equipment_grade(int equipped_item) {
    return equipped_item == 39 ? 2 : equipped_item == 40 ? 3 :
        equipped_item == 41 ? 2 : equipped_item == 42 ? 1 : 0;
}
constexpr int battle_officer_grade(int equipped_item,uint8_t tier2,uint8_t tier3,uint8_t tier4) {
    return battle_equipment_grade(equipped_item) + battle_skill_grade(tier2,tier3,tier4);
}
constexpr int commander_moving_action_interval(int own_grade,int opposing_grade) {
    return 37 + 2 * (opposing_grade - own_grade);
}
constexpr int soldier_attack_event_delay(int force,int side,bool rear) {
    // Native deployment selects M objects (IDs100+force / 140+force) for
    // attackers and W objects (120+force / 160+force) for defenders.
    // Their Things.ini Attack sequences dispatch OM_ATTACK at these ages.
    if(force == 4 && side == 0) return 20; // M5, front or mounted rear.
    if(force == 5 && ((side == 0 && !rear) || (side == 1 && rear))) return 22;
    return 15;
}
constexpr int experience_threshold(int level) {
    // 0x40a950: signed integer division, including the unusual level-zero value.
    return level <= 50 ? (((level-1)*137-34)*level)/2+17 : 99999;
}
constexpr int initial_level(uint32_t experience) {
    // 0x40a9e3: note that >= threshold(49) returns 50, not 49.
    for(int level = 0; level < 50; ++level)
        if(experience < uint32_t(experience_threshold(level))) return level-1;
    return 50;
}
constexpr int max_hp(int base, int strength, int level) {
    int value = base + level*2*strength/100;
    return value < base ? base : value > 999 ? 999 : value;
}
constexpr int max_mp(int base, int intelligence, int level) {
    int value = base + level*2*intelligence/100;
    return value < base ? base : value > 999 ? 999 : value;
}
constexpr int front_capacity(int level) {
    return level < 20 ? level*5 : level < 40 ? (40-level)*5 : 0;
}
constexpr int rear_capacity(int level) {
    int value = (level-20)*5;
    return value < 0 ? 0 : value > 100 ? 100 : value;
}
constexpr int skill_tiers(int level) { return level >= 40 ? 4 : level >= 20 ? 3 : level >= 10 ? 2 : 1; }
constexpr int recovery_interval(int defense, bool mp) {
    int base = mp ? 40 : 30;
    return base + base*(999-defense)/2997;
}
constexpr int reserve_capacity(uint32_t population) {
    int value = 110+int(population/2500);
    return value > 600 ? 600 : value;
}
// demake Informations.InitCityInfo assigns a fixed reservistMax per city.
constexpr int demake_reserve_capacity(int city) {
    constexpr int caps[city_count] = {
        140,146,179,145,252,242,195,161,176,198,280,166,
        233,145,158,174,176,185,238,206,210,240,195,258,
        211,162,203,153,199,179,200,171,226,202,165,178,
        195,182,156,204,233,204,194,180,231,169,176,134
    };
    return city >= 0 && city < city_count ? caps[city] : 0;
}
constexpr int demake_reservist_interval = 30; // demake calls AddReservist every 0.5 seconds.
constexpr int reserve_interval(uint32_t population,int defense) {
    int base = 4980/reserve_capacity(population);
    return base+base*(999-defense)/2997;
}
// Native 0x41DEE8 adds this amount to each city money field once per year.
constexpr uint32_t city_annual_income(uint32_t population) {
    return 0x8ca + (population * 0x4b) / 10000;
}
// StrategyController.OnTimeOverMode uses single-precision division then
// truncates the complete sum. Keep this distinct from the native-PC formula.
constexpr uint32_t demake_city_annual_income(uint32_t population) {
    return uint32_t(2250.0f+float(population)/133.34f);
}
constexpr int healing_amount(int script) { return script == 2666 ? 20 : script == 2667 ? 40 : 0; }
constexpr int reinforcement_amount(int script) {
    // demake MagicAmbush.levelNum, in the game's four skill-tier order.
    return script == 2669 ? 4 : script == 2670 ? 8 : script == 2672 ? 16 : script == 2673 ? 30 : 0;
}
constexpr int reinforcement_limit(int level) { return (level < 20 ? level : 20)*5; }
constexpr int reinforcement_requested_count(int script,int general_level,int current_soldiers) {
    const int amount = reinforcement_amount(script);
    const int available = reinforcement_limit(general_level)-current_soldiers;
    return amount < available ? amount : available > 0 ? available : 0;
}
enum class MagicTarget : uint8_t {
    enemy_force, enemy_general, enemy_soldiers, both_soldiers, enemy_general_and_soldiers,
    visual_only,
    minefield
};
constexpr MagicTarget magic_target(int script) {
    switch(script) {
    case 2608: case 2611:
        return MagicTarget::enemy_general_and_soldiers;
    case 2612: case 2613: case 2614:
        return MagicTarget::minefield;
    case 2617:
        return MagicTarget::visual_only;
    case 2618: case 2622: case 2623:
        return MagicTarget::enemy_general_and_soldiers;
    case 2660: case 2633: case 2634:
        return MagicTarget::enemy_general;
    case 2635:
        return MagicTarget::enemy_general_and_soldiers;
    case 2645: case 2646: case 2647:
    case 2630: case 2631: case 2632: case 2642: case 2643: case 2644:
    case 2639: case 2640: case 2641:
        return MagicTarget::enemy_general_and_soldiers;
    case 2626: case 2665:
        return MagicTarget::enemy_general;
    case 2624: case 2625: case 2627: case 2628: case 2650:
        return MagicTarget::enemy_soldiers;
    case 2636: case 2637: case 2638:
        return MagicTarget::enemy_soldiers;
    default:
        return MagicTarget::enemy_force;
    }
}
constexpr int minefield_count(int script) {
    switch(script) {
    case 2612: return 10;
    case 2613: return 20;
    case 2614: return 40;
    default: return 0;
    }
}
// Things.ini sequences3500/3501 show seven M005Fire frames at two updates
// each, repeating frame seven for the eighth slot before removing the object.
constexpr int minefield_visual_lifetime = 16;
// Projectiles that cut down the soldiers they sweep through. The M011 crescent
// slash (2630-2632) and the ghost halberd (2635) both target the general and
// enemy soldiers; the bow shots (2633 sunset bow, 2634, 2660 falling moon bow)
// target the general alone and pass through soldier cells untouched.
constexpr bool magic_projectile_cuts_soldiers(int script) {
    return (script >= 2630 && script <= 2632) || script == 2635;
}
constexpr bool magic_uses_collision_projectile(int script) {
    return script == 2660 || script == 2633 || script == 2634 || script == 2635 ||
        script == 2630 || script == 2631 || script == 2632 ||
        script == 2642 || script == 2643 || script == 2644 ||
        (script >= 2645 && script <= 2647);
}
// demake FireDragon level selects one, two or three simultaneous vertical lanes.
constexpr int dragon_projectile_count(int script) {
    return script == 2642 ? 1 : script == 2643 ? 2 : script == 2644 ? 3 : 0;
}
constexpr int dragon_projectile_interval(int script) {
    (void)script;
    return 0; // demake generates all vertical lanes together, not delayed shots.
}
constexpr bool magic_uses_tornado(int script) {
    return script == 2636 || script == 2637 || script == 2638;
}
constexpr int tornado_count(int script) {
    return magic_uses_tornado(script) ? script-2635 : 0;
}
constexpr int sword_wave_projectile_count(int script) {
    return script == 2639 ? 4 : script == 2640 ? 8 : script == 2641 ? 16 : 0;
}
constexpr int lightning_strike_count(int script) {
    return script == 2624 ? 22 : script == 2625 ? 44 : 0;
}
constexpr int rockfall_strike_count(int script) {
    return script == 2618 ? 3 : script == 2622 ? 7 : script == 2623 ? 12 : 0;
}
  // MAGIC01 3902 starts two GLOBAL2505 groups. Each creates one stationary
  // object; its local counter4 through0 updates that same object's position.
constexpr int stationary_callback_object_count(int script) {
      return script == 2626 ? 2 : 0;
}
constexpr int stationary_callback_windup(int script) {
    return script == 2626 ? 42 : 0;
}
// GLOBAL 6230 is the level-40 scholar ultimate. It lays eight horizontally
// moving collision objects across the outer formation rows, then sends two
// fixed-damage thunder objects to the opposing general.
constexpr bool magic_uses_heaven_and_earth(int script) { return script == 2663; }
constexpr bool magic_uses_ghost_crying_god(int script) { return script == 2664; }
constexpr int ghost_crying_god_projectile_count(int script) {
    return magic_uses_ghost_crying_god(script) ? 20 : 0;
}
constexpr int ghost_crying_god_siege_visual_count(int script) {
    return magic_uses_ghost_crying_god(script) ? 7 : 0;
}
constexpr int ghost_crying_god_grid_count(int script) {
    return magic_uses_ghost_crying_god(script) ? 20 : 0;
}
constexpr int ghost_crying_god_arrow_visual_count(int script) {
    return magic_uses_ghost_crying_god(script) ? 20 : 0;
}
constexpr int ghost_crying_god_visual_count(int script) {
    // Seven siege visuals, twenty M001 placeholder/animation/fire triplets,
    // and twenty 3102 trails.
    return ghost_crying_god_siege_visual_count(script) +
           3*ghost_crying_god_grid_count(script) +
           ghost_crying_god_arrow_visual_count(script);
}
constexpr int heaven_and_earth_beam_count(int script) {
    return magic_uses_heaven_and_earth(script) ? 8 : 0;
}
constexpr int heaven_and_earth_beam_lifetime(int script) {
    // GLOBAL 2568 waits 300 updates, then native38(16,1) starts the
    // object's final 16-update timer before deletion.
    return magic_uses_heaven_and_earth(script) ? 316 : 0;
}
constexpr int heaven_and_earth_general_strike_count(int script) {
    return magic_uses_heaven_and_earth(script) ? 2 : 0;
}
constexpr int heaven_and_earth_general_checks(int script) {
    return magic_uses_heaven_and_earth(script) ? 5 : 0;
}
constexpr int heaven_and_earth_beam_launch_delay(int script) {
    return magic_uses_heaven_and_earth(script) ? 215 : 0;
}
constexpr int heaven_and_earth_general_launch_delay(int script) {
    return magic_uses_heaven_and_earth(script) ? 220 : 0;
}
constexpr int magic_projectile_launch_delay(int script) {
    // GLOBAL 4100/4600 wait 15 + 10; GLOBAL 4200 waits 15; the dragon's
    // MAGIC01 4500 path reaches projectile setup after 20 + 30 updates.
    if((script >= 2630 && script <= 2632) || (script >= 2645 && script <= 2647) ||
       script == 2664) return 25;
    // Tornado target selection is at WAIT20. The 15+12 setup waits before
    // motion are modeled separately after selecting the target.
    if(magic_uses_tornado(script)) return 20;
    return script == 2642 || script == 2643 || script == 2644 ? 50 : 15;
}
constexpr bool magic_uses_arrow_volley(int script) {
    return script >= 2600 && script <= 2605;
}
constexpr int fire_wheel_object_count(int script) {
    return script == 2608 || script == 2611 ? 8 : 0;
}
constexpr bool magic_uses_fire_wheel(int script) {
    return fire_wheel_object_count(script) != 0;
}
constexpr int fire_wheel_active_updates = 16;
constexpr int arrow_volley_count(int script) {
    switch(script) {
    case 2600: case 2603: return 10;
    case 2601: case 2604: return 20;
    case 2602: case 2605: return 40;
    default: return 0;
    }
}
// For demake-replicated direct general skills, ATTACK values in
// MagicConfig.xml are the authoritative callback damage.  Other entries
// retain their recovered native wrapper/global-44 values below.
constexpr int magic_general_hit_bonus(int script) {
    switch(script) {
    case 2608: return 8;
    case 2611: return 10;
    case 2618: return 8;
    case 2622: case 2623: return 12;
    case 2612: case 2613: case 2614: return 5;
    case 2617: return 12; // demake MagicConfig.xml sequence14 fire damage.
    case 2636: case 2637: case 2638: return 8; // demake Tornado region damage.
    case 2635: return 8; // demake MagicConfig.xml: 鬼戟.
    case 2642: case 2643: case 2644: return 10;
    case 2639: case 2640: case 2641: return 10; // GLOBAL4400 uses GLOBAL2521.
    case 2630: return 12;
    case 2631: return 14;
    case 2632: return 16;
    // GLOBAL 2663 initializes shared global44 to 30; GLOBAL 2520 changes it
    // to 1 after the first accepted general callback.
    case 2663: return 30;
    case 2600: case 2601: case 2602: return 5;
    case 2603: case 2604: case 2605: return 8;
    case 2660: return 14; // demake MagicConfig.xml: 落月弓.
    case 2634: return 18;
    case 2657: case 2658: return 10;
    case 2659: return 12;
    case 2626: return 40;
    case 2633: return 16; // demake MagicConfig.xml: 落日弓.
    case 2645: case 2646: case 2647: return 30;
    case 2664: return 40;
    case 2665: return 50; // Config ATTACK; cast path expands this to 20+10+10+10.
    default: return 0;
    }
}
// Global collision callbacks mutate global44 after applying it. GLOBAL 2520
// stores 1; GLOBAL 2521 stores 2. MAGIC01 3103 also starts GLOBAL 2520 after
// its caster-scaled native293 hit.
int magic_general_callback_next_base(int script);
constexpr int magic_general_hit_damage(int script,int caster_level) {
    const int base=magic_general_hit_bonus(script);
    // GLOBAL6200's mode-1 child scripts overwrite callback2520 with MAGIC013103.
    return magic_uses_ghost_crying_god(script) ? base + base*caster_level/100 : base;
}
constexpr int cast_charge_max = 120000;
// demake WSInfoPanel initializes the 132-pixel sword meter at 60 and resets it
// to zero after every cast. Battle stores the meter on a 0..120000 scale.
constexpr int cast_charge_initial = cast_charge_max * 60 / 132;
constexpr int cast_charge_cost(int power) { return power*cast_charge_max/100; }
// WSInfoPanel adds dt * (STR / 30 + 3) meter pixels per frame. At the GBA
// battle loop's 30 updates/second, convert that rate to the stored scale.
constexpr int battle_charge_increment(int strength) { return (strength + 90) * 100 / 99; }
constexpr int skill_candidate(int tier,int choice) { return tier == 1 ? 1+choice : 4+choice; }

// The native 55-record item pool at 0x469B3C. Search uses each record's
// weight at +4; records with kind 2 and used flag at +6 have zero weight.
constexpr uint8_t item_kinds[55] = {
    2,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    0,0,0,0,0,0,0,0,2,2,2,0,1,1,1,1,1,1,1,0,0,0,0,0
};
constexpr uint8_t item_weights[55] = {
    0,100,100,80,100,100,100,100,80,100,80,50,
    5,5,5,5,5,5,5,5,5,5,5,5,5,10,10,10,10,10,10,
    50,50,50,50,50,100,100,100,5,5,5,20,100,100,100,100,100,100,100,
    0,0,0,0,0
};
constexpr bool unique_search_item(int id) { return id >= 0 && id < 55 && item_kinds[id] == 2; }
constexpr uint8_t search_item_weight(int id,bool used) {
    return unique_search_item(id) && used ? 0 : item_weights[id];
}

constexpr int item_strength_bonus(int id) {
    constexpr uint8_t bonuses[19] = {10,10,9,9,8,5,7,7,6,6,6,4,4,4,4,4,2,2,2};
    return id >= 20 && id <= 38 ? bonuses[id-20] : 0;
}
constexpr int item_intelligence_bonus(int id) {
    constexpr uint8_t bonuses[5] = {10,9,9,8,7};
    return id >= 12 && id <= 16 ? bonuses[id-12] : 0;
}
constexpr int item_max_hp_bonus(int id) { return id == 18 ? 10 : 0; }
constexpr int item_max_mp_bonus(int id) { return id == 17 || id == 19 ? 10 : 0; }

// Item helper tables at 0x46EB78/0x46EB58 name troop types and formations.
// The troop list's label order differs from the FORCE token order in the
// scenario data, so map the item IDs to the matching force-mask bit.
constexpr int item_force_type(int id) {
    constexpr uint8_t types[11] = {0,1,3,2,5,4,9,7,10,8,6};
    return id >= 1 && id <= 11 ? types[id-1] : -1;
}
constexpr int item_formation(int id) { return id >= 43 && id <= 49 ? id-43 : -1; }

// Native 0x428CC1 selects the lowest set bit from each force mask and reads
// this signed 11x11 table at 0x470918. Rows are the attacker's force; columns
// are the defender's. The result is added to the base soldier hit threshold.
constexpr int8_t force_hit_modifiers[11][11] = {
    {  0,-33,-50, 33, 31, 50,-47,  9,-34, -6, 19 },
    { 33,  0,-12,-32,-30,-50,-48, 35,  7, 49,  6 },
    { 50, 12,  0,-32,-19,-28, 17, 15,-15, 17, 17 },
    {-33, 32, 32,  0,-11, 11,-45,-33, 47,-45, -6 },
    {-31, 30, 19, 11,  0, -9, 26,-37, 35,-18, -4 },
    {-50, 50, 28,-11,  9,  0, 29,-12, 16,-30, 19 },
    { 47, 48,-17, 45,-26,-29,  0,-18,-15,-35, 29 },
    { -9,-35,-15, 33, 37, 12, 18,  0,-35, 12, -6 },
    { 34, -7, 15,-47,-35,-16, 15, 35,  0, 21,  5 },
    {  6,-49,-17, 45, 18, 30, 35,-12,-21,  0, -7 },
    {-19, -6,-17,  6,  4,-19,-29,  6, -5,  7,  0 }
};
constexpr int force_hit_modifier(int attacker_force,int defender_force) {
    return attacker_force >= 0 && attacker_force < 11 && defender_force >= 0 && defender_force < 11 ?
        force_hit_modifiers[attacker_force][defender_force] : 0;
}
constexpr int soldier_hit_threshold(int attacker_force,int defender_force) {
    return 150 + force_hit_modifier(attacker_force,defender_force);
}

// SANGO.exe 0x40d6a2 samples the China map at half-resolution, then
// 0x428d09 adds the force's signed context value from 0x470afc.
constexpr int8_t force_context_modifiers[unit_types][9] = {
    {  0,  0,  0, -3,  0,  0,  3,  7,-14 },
    {-13,  0,  0, -3,  3,  0,  5, -2,-13 },
    {-14,  0,  0, -3,  4,  0, -1, -2,-12 },
    {-17,  0,  0,  7,  0,  0,  2, -5,-15 },
    {-12,  0,  0,  5,  0,  0, -2, -4,-12 },
    {-15,  0,  0,  5,  0,  0,  7, -4,-17 },
    {-12,  0,  0, -5,  0,  0, 14, 12,-17 },
    { -2,  0,  0,-19,  0,  0, 20,  8,-20 },
    {  6,  0,  0, -7,  3,  0,  5,  2,-10 },
    {-17,  0,  0,-18,  0,  0,  9,  8, 20 },
    {-20,  0,  0,-14,  0,  0, 19, 16, -3 }
};
constexpr int force_context_modifier(int force,int context) {
    return force >= 0 && force < unit_types && context >= 0 && context < 9 ?
        force_context_modifiers[force][context] : 0;
}

inline uint8_t terrain_at(int world_x,int world_y) {
    const int x = world_x / 2 - terrain_map_left;
    const int y = world_y / 2 - terrain_map_top;
    if(x < 0 || y < 0 || x >= terrain_map_width || y >= terrain_map_height) return 5;
    int remaining = y * int(terrain_map_width) + x;
    uint32_t offset = 0;
    uint8_t value = 5;
    while(remaining >= 0 && offset < terrain_mask_data_size) {
        const uint8_t code = terrain_mask_data[offset++];
        int step;
        if((code & 0xc0) == 0) {
            step = 1;
            value = code;
        } else {
            step = code & 0x3f;
            if(offset >= terrain_mask_data_size) return 5;
            value = terrain_mask_data[offset++];
        }
        remaining -= step;
    }
    return remaining < 0 && value == 0xff ? 5 : value;
}

constexpr int soldier_hit_threshold(int attacker_force,int defender_force,int context) {
    return soldier_hit_threshold(attacker_force,defender_force) +
        force_context_modifier(attacker_force,context);
}
constexpr int soldier_hit_threshold(int attacker_force,int defender_force,int context,int side_field) {
    return soldier_hit_threshold(attacker_force,defender_force,context) + (side_field + 50) / 100;
}
// Native 0x43e34b: rear-row attackers (object flag 0x4) get a second
// RNG(300) check after a miss, at floor(2*threshold/3).
constexpr int soldier_followup_threshold(int threshold) { return (threshold * 2) / 3; }
constexpr bool soldier_hit_rolls(int threshold,bool rear_attacker,int first_roll,int followup_roll) {
    return first_roll < threshold ||
        (rear_attacker && followup_roll < soldier_followup_threshold(threshold));
}
// Native 0x43f80f: rear-rank soldiers deal 120% of their side-field damage
// before converting it to the integer hit-point delta.
constexpr int soldier_general_damage(int side_field,bool rear_attacker) {
    const int adjusted = rear_attacker ? side_field * 120 / 100 : side_field;
    return (adjusted * 4) / 192;
}
constexpr int composite_projectile_reservation(int script) {
    // All three overlapping children, including delayed-launch scheduler.
    // Heaven: 20 yellow-dragon +8 cart objects +1 scheduler +2 thunder.
    // Ghost: 20 fire arrows*4 +7 fire stones*15 +1 scheduler +1 clone.
    return script==2663 ? 31 : script==2664 ? 187 : 0;
}
}
#endif
