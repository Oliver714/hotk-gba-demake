#ifndef SANGO_MODEL_H
#define SANGO_MODEL_H
#include "data.h"
namespace sg {
constexpr int battle_grid_columns = 56;
constexpr int battle_grid_rows = 14;
constexpr int battle_grid_column_width = 40;
constexpr int battle_grid_row_height = 30;
constexpr int battlefield_width = battle_grid_columns * battle_grid_column_width;
constexpr int battle_general_front_offset = battle_grid_column_width * 12;
constexpr int battle_view_width = 240;
constexpr uint32_t city_population_cap = 999999;
constexpr int city_defense_cap = 9999;
// Tile-aligned field lets the GBA map hardware mirror the player view.
constexpr int battle_view_top = 48;
constexpr int battle_view_bottom = 144;
constexpr int battle_view_height = battle_view_bottom-battle_view_top;
constexpr int battle_view_center_y = (battle_view_top+battle_view_bottom)/2;
constexpr int battle_grid_max_y = (battle_grid_rows - 1) * battle_grid_row_height;
constexpr int battle_commander_row = 6;
constexpr int battle_commander_y = battle_commander_row * battle_grid_row_height;
constexpr int battle_retreat_margin = battle_grid_column_width * 16;
constexpr int max_battle_generals = 10;
constexpr int max_battle_captured = max_battle_generals * 2;
constexpr int battle_projectile_slots = 384;
constexpr int battle_m014_spark_emitters = 30;
// Native FORCE token order differs from demake arms-mask order: bow=2,
// spear=3. Share this classification between tactical AI and attack logic.
constexpr bool is_ranged_force(int force) { return force==2 || force==4 || force==5; }
template<class T> constexpr T minimum(T a, T b) { return a < b ? a : b; }
template<class T> constexpr T maximum(T a, T b) { return a > b ? a : b; }
template<class T> constexpr T clamp(T v, T lo, T hi) { return minimum(hi, maximum(lo, v)); }
enum class Phase : uint8_t { domestic, world, battle, prisoners, ended };
enum class Status : uint8_t { absent, wild, employed, prisoner };
enum class Order : uint8_t { hold, advance, front, rear, general, retreat, front_back };
// Match demake's BattleCommand button order, omitting magic (which has its own
// skill selector in the GBA HUD): front run/back, disperse/fall-in, idle,
// army assault, general assault, escape.
enum class BattleCommand : uint8_t { front_run, front_back, disperse, fall_in, hold, assault, general_assault, retreat };
struct Officer {
    int16_t city, army;
    int8_t king;
    Status status;
    uint16_t hp, mp, soldiers, front, forces;
    uint32_t exp;
    uint16_t hp_ticks, mp_ticks;
    uint8_t rank, learned[4];
    uint8_t loyalty, force, formations, formation, worked;
    // First 14 bytes are legacy personal inventory storage in saves <=12.
    // Newer saves reuse these bytes for the compact city object pool.
    uint8_t item_count, packed_items[13], equipped_item;
    // These bytes replace the old derived equipment bonuses. Store the cached
    // caps as little-endian byte pairs to preserve the packed SRAM record size.
    uint8_t level_hp_cap_low, level_hp_cap_high, level_mp_cap_low, level_mp_cap_high;
    uint8_t item_flags[3];
};
inline uint16_t officer_level_hp_cap(const Officer& officer) {
    return uint16_t(officer.level_hp_cap_low | (uint16_t(officer.level_hp_cap_high) << 8));
}
inline uint16_t officer_level_mp_cap(const Officer& officer) {
    return uint16_t(officer.level_mp_cap_low | (uint16_t(officer.level_mp_cap_high) << 8));
}
inline void set_officer_level_hp_cap(Officer& officer,uint16_t value) {
    officer.level_hp_cap_low = uint8_t(value);
    officer.level_hp_cap_high = uint8_t(value >> 8);
}
inline void set_officer_level_mp_cap(Officer& officer,uint16_t value) {
    officer.level_mp_cap_low = uint8_t(value);
    officer.level_mp_cap_high = uint8_t(value >> 8);
}
inline int inventory_count(const Officer& officer) { return officer.item_count; }
inline int inventory_item(const Officer& officer,int index) {
    if(index < 0 || index >= officer.item_count || index >= 17) return -1;
    const int bit = index * 6, byte = bit >> 3, shift = bit & 7;
    uint16_t pair = officer.packed_items[byte];
    if(byte + 1 < 13) pair |= uint16_t(officer.packed_items[byte+1]) << 8;
    return (pair >> shift) & 0x3f;
}
inline void set_inventory_item(Officer& officer,int index,uint8_t item) {
    if(index < 0 || index >= 17 || item >= 64) return;
    const int bit = index * 6, byte = bit >> 3, shift = bit & 7;
    uint16_t pair = officer.packed_items[byte];
    if(byte + 1 < 13) pair |= uint16_t(officer.packed_items[byte+1]) << 8;
    pair = uint16_t((pair & ~(uint16_t(0x3f) << shift)) | (uint16_t(item) << shift));
    officer.packed_items[byte] = uint8_t(pair);
    if(byte + 1 < 13) officer.packed_items[byte+1] = uint8_t(pair >> 8);
}
inline bool append_inventory_item(Officer& officer,uint8_t item) {
    if(officer.item_count >= 17 || item >= 64) return false;
    set_inventory_item(officer,officer.item_count,item);
    ++officer.item_count;
    return true;
}
inline void remove_inventory_item(Officer& officer,int index) {
    if(index < 0 || index >= officer.item_count) return;
    for(int i=index;i+1<officer.item_count;++i) set_inventory_item(officer,i,uint8_t(inventory_item(officer,i+1)));
    --officer.item_count;
    set_inventory_item(officer,officer.item_count,0);
}
struct City {
    int8_t king;
    int16_t major;
    uint32_t population;
    uint16_t defense, reserve, reserve_ticks;
    uint32_t money;
};
struct Army {
    int16_t generals[5];
    int8_t king, origin, target;
    uint8_t count, active;
    uint16_t remaining, total;
    uint32_t money;
    int8_t retreat_goal;
    uint8_t retreating;
    uint8_t garrisoned;
};
struct Campaign {
    uint32_t random;
    uint16_t year, day, month_ticks;
    uint8_t scenario, player, month;
    uint16_t ai_cooldown[max_kings];
    Phase phase;
    Officer officers[general_count];
    City cities[city_count];
    Army armies[max_armies];
    uint8_t item_used[55];
    uint32_t random_secondary;
    uint32_t faction_experience[max_kings];
};
// Reuse the compact legacy inventory bytes as a city-owned object pool. Counts
// occupy the first city_count bytes; entries follow by city in a packed stream.
// Native recruitment metadata stays in the final bytes of this same region.
inline uint8_t& faction_storage_byte(Campaign& c,int offset) {
    Officer& o=c.officers[offset/14];
    return offset%14 ? o.packed_items[offset%14-1] : o.item_count;
}
inline uint8_t faction_storage_byte(const Campaign& c,int offset) {
    const Officer& o=c.officers[offset/14];
    return offset%14 ? o.packed_items[offset%14-1] : o.item_count;
}
constexpr int faction_item_storage_bytes = max_kings * 55 * 2;
constexpr int city_object_capacity = 50;
constexpr int city_search_inventory_limit = 50;
constexpr int faction_escape_storage_bytes = (general_count * 3 + 7) / 8;
constexpr int army_destination_storage = faction_item_storage_bytes + general_count * 2 + faction_escape_storage_bytes;
static_assert(army_destination_storage + max_armies <= general_count * 14,
              "Native officer state must fit in unused faction item storage");
// Low seven bits: zero for an ordinary route, or next destination + 1.
// High bit preserves the original road geometry when reversing direction.
// Lives in reserved, saved inventory padding; no save layout or RAM growth.
inline uint8_t& army_pending_destination(Campaign& c,int army) {
    return faction_storage_byte(c,army_destination_storage+army);
}
inline int army_destination(const Campaign& c,int army) {
    const int pending=faction_storage_byte(c,army_destination_storage+army)&0x7f;
    return pending ? pending-1 : c.armies[army].target;
}
struct MapPoint { int x,y; };
inline bool army_reverse_geometry(const Campaign& c,int army) {
    return (faction_storage_byte(c,army_destination_storage+army)&0x80)!=0;
}
MapPoint army_route_position(const Army& army,uint32_t elapsed_ticks,bool reverse_geometry=false);
int map_route_cities(int from,int to,int* cities,int capacity);
inline MapPoint army_position(const Campaign& c,int index) {
    const auto& a=c.armies[index];
    return army_route_position(a,uint32_t(a.total-a.remaining),army_reverse_geometry(c,index));
}
inline int city_inventory_count(const Campaign& c,int city) {
    return city>=0 && city<city_count ? faction_storage_byte(c,city) : 0;
}
inline int city_inventory_total(const Campaign& c) {
    int total=0;
    for(int city=0;city<city_count;++city) total+=city_inventory_count(c,city);
    return total;
}
inline int city_inventory_start(const Campaign& c,int city) {
    int offset=city_count;
    for(int i=0;i<city;++i) offset+=city_inventory_count(c,i);
    return offset;
}
inline uint8_t city_inventory_type(uint8_t entry) { return entry >> 6; }
inline uint8_t city_inventory_index(uint8_t entry) { return entry & 0x3f; }
inline uint8_t city_inventory_entry(int type,int index) {
    return type>=0 && type<=3 && index>=0 && index<64 ? uint8_t((type<<6)|index) : 0;
}
inline uint8_t city_inventory_item(const Campaign& c,int city,int index) {
    if(city<0 || city>=city_count || index<0 || index>=city_inventory_count(c,city)) return 0;
    return faction_storage_byte(c,city_inventory_start(c,city)+index);
}
inline bool append_city_inventory_item(Campaign& c,int city,int type,int index) {
    const int total=city_inventory_total(c);
    const int max_total=faction_item_storage_bytes-city_count;
    if(city<0 || city>=city_count || c.cities[city].king<0 ||
       city_inventory_count(c,city)>=city_object_capacity || total>=max_total) return false;
    if(type<0 || type>3 || index<0 || index>=64) return false;
    const int offset=city_inventory_start(c,city)+city_inventory_count(c,city);
    for(int i=total;i>offset-city_count;--i)
        faction_storage_byte(c,city_count+i)=faction_storage_byte(c,city_count+i-1);
    faction_storage_byte(c,offset)=city_inventory_entry(type,index);
    ++faction_storage_byte(c,city);
    return true;
}
inline bool replace_city_inventory_item(Campaign& c,int city,int item_index,int type,int index) {
    if(city<0 || city>=city_count || item_index<0 || item_index>=city_inventory_count(c,city) ||
       type<0 || type>3 || index<0 || index>=64) return false;
    faction_storage_byte(c,city_inventory_start(c,city)+item_index)=city_inventory_entry(type,index);
    return true;
}
inline void remove_city_inventory_item(Campaign& c,int city,int index) {
    if(city<0 || city>=city_count || index<0 || index>=city_inventory_count(c,city)) return;
    const int total=city_inventory_total(c),offset=city_inventory_start(c,city)+index;
    for(int i=offset;i<city_count+total-1;++i)
        faction_storage_byte(c,i)=faction_storage_byte(c,i+1);
    --faction_storage_byte(c,city);
    faction_storage_byte(c,city_count+total-1)=0;
}
inline void clear_city_inventory_storage(Campaign& c) {
    for(int i=0;i<faction_item_storage_bytes;++i) faction_storage_byte(c,i)=0;
}
inline int city_object_legacy_item(uint8_t entry) {
    const int type=city_inventory_type(entry),index=city_inventory_index(entry);
    if(type==0) return index;
    if(type==1 && index<31) return 12+index;
    if(type==2 && index<11) return index+1;
    if(type==3 && index<7) return 43+index;
    return -1;
}
inline uint8_t legacy_item_city_object(int item) {
    if(item>=1 && item<=11) return city_inventory_entry(2,item-1);
    if(item>=12 && item<=42) return city_inventory_entry(1,item-12);
    if(item>=43 && item<=49) return city_inventory_entry(3,item-43);
    if(item>=0 && item<55) return city_inventory_entry(0,item);
    return 0;
}
inline uint8_t& native_origin_king(Campaign& c,int general) {
    return faction_storage_byte(c,faction_item_storage_bytes+general);
}
inline uint8_t native_origin_king(const Campaign& c,int general) {
    return faction_storage_byte(c,faction_item_storage_bytes+general);
}
inline uint8_t& native_status2_countdown(Campaign& c,int general) {
    return faction_storage_byte(c,faction_item_storage_bytes+general_count+general);
}
inline uint8_t native_status2_countdown(const Campaign& c,int general) {
    return faction_storage_byte(c,faction_item_storage_bytes+general_count+general);
}
inline uint8_t native_escape_count(const Campaign& c,int general) {
    const int bit=general*3,byte=bit/8,shift=bit%8;
    const int offset=faction_item_storage_bytes+general_count*2+byte;
    uint16_t packed=faction_storage_byte(c,offset);
    if(shift>5) packed|=uint16_t(faction_storage_byte(c,offset+1))<<8;
    return uint8_t((packed>>shift)&7);
}
inline void set_native_escape_count(Campaign& c,int general,int value) {
    const int bit=general*3,byte=bit/8,shift=bit%8;
    const int offset=faction_item_storage_bytes+general_count*2+byte;
    uint16_t packed=faction_storage_byte(c,offset);
    if(shift>5) packed|=uint16_t(faction_storage_byte(c,offset+1))<<8;
    const uint16_t mask=uint16_t(7u<<shift);
    packed=uint16_t((packed&~mask)|((uint16_t(value&7))<<shift));
    faction_storage_byte(c,offset)=uint8_t(packed);
    if(shift>5) faction_storage_byte(c,offset+1)=uint8_t(packed>>8);
}
inline int faction_item_quantity(const Campaign& c,int king,int item) {
    int count=0;
    for(int city=0;city<city_count;++city) if(c.cities[city].king==king)
        for(int i=0;i<city_inventory_count(c,city);++i)
            count+=city_object_legacy_item(city_inventory_item(c,city,i))==item;
    return count;
}
inline void remove_faction_item(Campaign& c,int king,int item);
inline bool append_faction_item(Campaign& c,int king,int item);
inline void set_faction_item_quantity(Campaign& c,int king,int item,int quantity) {
    while(faction_item_quantity(c,king,item)<quantity)
        if(!append_faction_item(c,king,item)) break;
    while(faction_item_quantity(c,king,item)>quantity) remove_faction_item(c,king,item);
}
inline int faction_inventory_count(const Campaign& c,int king) {
    int count=0;
    for(int city=0;city<city_count;++city) if(c.cities[city].king==king) count+=city_inventory_count(c,city);
    return count;
}
inline int faction_inventory_item(const Campaign& c,int king,int index) {
    if(index<0) return -1;
    for(int city=0;city<city_count;++city) if(c.cities[city].king==king) {
        const int count=city_inventory_count(c,city);
        if(index<count) return city_object_legacy_item(city_inventory_item(c,city,index));
        index-=count;
    }
    return -1;
}
inline bool append_faction_item(Campaign& c,int king,int item) {
    const uint8_t entry=legacy_item_city_object(item);
    if(king<0 || king>=max_kings || !entry) return false;
    for(int city=0;city<city_count;++city) if(c.cities[city].king==king &&
        city_inventory_count(c,city)<city_object_capacity &&
        city_inventory_total(c)<faction_item_storage_bytes-city_count)
        return append_city_inventory_item(c,city,city_inventory_type(entry),city_inventory_index(entry));
    return false;
}
inline void remove_faction_item(Campaign& c,int king,int item) {
    for(int city=0;city<city_count;++city) if(c.cities[city].king==king)
        for(int i=0;i<city_inventory_count(c,city);++i)
            if(city_object_legacy_item(city_inventory_item(c,city,i))==item) { remove_city_inventory_item(c,city,i); return; }
}
// Migrates the old per-officer bags after their save checksum has been verified.
inline bool migrate_faction_items(Campaign& c) {
    uint16_t quantities[max_kings][55]{};
    for(int king=0;king<max_kings;++king) for(int item=0;item<55;++item) {
        const int offset=(king*55+item)*2;
        quantities[king][item]=uint16_t(faction_storage_byte(c,offset) |
            (uint16_t(faction_storage_byte(c,offset+1))<<8));
    }
    clear_city_inventory_storage(c);
    for(int king=0;king<max_kings;++king) for(int item=0;item<55;++item)
        for(int i=0;i<quantities[king][item];++i) append_faction_item(c,king,item);
    return true;
}
inline bool migrate_personal_items(Campaign& c) {
    uint16_t quantities[max_kings][55]{};
    for(const Officer& o:c.officers) {
        if(o.item_count>17 || (o.equipped_item && o.item_count>16)) return false;
        for(int i=0;i<o.item_count;++i) {
            const int item=inventory_item(o,i);
            if(item<0 || item>=55 || o.king < -1 || o.king>=max_kings || c.player>=max_kings) return false;
            ++quantities[o.king>=0 ? o.king : c.player][item];
        }
    }
    clear_city_inventory_storage(c);
    for(Officer& o:c.officers) { o.item_count=0; for(auto& b:o.packed_items) b=0; }
    for(int king=0;king<max_kings;++king)
        for(int item=0;item<55;++item)
            for(int i=0;i<quantities[king][item];++i) append_faction_item(c,king,item);
    return true;
}
static_assert(max_kings*55*2<=general_count*14);
struct Fighter {
    int16_t general, hp, mp, soldiers, front, x, previous_x, soldier_hit_threshold, side_bonus;
    int16_t initial_hp, initial_soldiers;
    uint32_t charge;
    uint32_t ai_action_flags;
    uint32_t ai_last_action_tick;
    uint8_t ai_command_slots[8];
    uint8_t force;
    Order order;
    uint8_t attack_ticks, move_command8;
    uint16_t action_ticks, action_delay;
    uint8_t facing;
    int8_t attack_direction;
    int16_t deployment_x;
};
// demake's GeneralController keeps isMightyHit set after a type-4 front
// attack. ai_action_flags only consumes its low three bits, so preserve the
// state here without changing the transient Fighter layout.
constexpr uint32_t fighter_mighty_hit_flag = 0x80000000u;
constexpr uint32_t fighter_opening_checked_flag=1u<<3;
constexpr uint32_t fighter_opening_finished_flag=1u<<4;
constexpr uint32_t fighter_attack_cooldown_flag=1u<<5;
constexpr uint32_t fighter_escaped_flag=1u<<6;
constexpr uint32_t fighter_idle_timer_mask=0x1ff00u;
struct Soldier {
    int16_t x, y, previous_x, previous_y;
    int8_t hp;
    uint8_t side, index, rear, formation_column;
    int8_t attack_direction;
    int16_t move_target_x, move_target_y;
    int8_t move_dx, move_dy, move_x_remainder, move_y_remainder;
    uint8_t move_ticks, move_command8, attack_ticks;
    uint16_t action_ticks, action_delay;
};
// Formation column 2 is the reserve/rear deployment group. `rear` instead
// denotes the veteran/knight class used by capacity and damage calculations;
// either class can be deployed in the tactical front rank (demake's lists).
inline bool soldier_in_front_rank(const Soldier& soldier) {
    return soldier.formation_column!=2;
}
struct Projectile {
    int16_t x, y, dx;
    int16_t source_script;
    int16_t height, target_offset_x;
    int8_t target_side;
    int8_t movement_remainder, height_remainder;
    uint8_t attacker_rear, owner_index;
    uint8_t active, kind, collision_enabled, remaining, magic;
    uint8_t phase, scatter;
};
struct M014SparkEmitter {
    int16_t x, y;
    uint16_t start_tick;
    uint8_t active;
    uint8_t variants[20], azimuths[20], elevations[20];
};
// Display-only animation state. Kept outside Fighter/Soldier so combat
// layouts used by native comparison tools and save fixtures stay unchanged.
struct ActorVisual {
    uint8_t facing, motion_hold, walk_tick, walk_frame;
};
void update_actor_visual(ActorVisual& visual,int dx,int dy,bool interpolating);
int magic_particle_velocity(int speed,int azimuth,int elevation,int axis);
int magic_planar_particle_velocity(int speed,int azimuth,int axis);
int magic_native_polar_offset(int radius,int angle);
int magic_native_gravity_height_eighth(int vertical_speed,int age);
int magic_native_vertical_velocity_q16(int speed,int elevation);
struct Battle {
    Fighter sides[2];
    // Entry health after the once-per-bout recovery, not maximum health.
    uint16_t starting_hp[2];
    Soldier soldiers[400];
    Projectile projectiles[battle_projectile_slots];
    M014SparkEmitter m014_spark_emitters[battle_m014_spark_emitters];
    int16_t attackers[max_battle_generals], defenders[max_battle_generals], captured[max_battle_captured];
    uint16_t starting_front[2][max_battle_generals], starting_rear[2][max_battle_generals];
    uint32_t experience_rewards[2][max_battle_generals];
    uint8_t layout_front[2][max_battle_generals];
    uint8_t general_front[2][max_battle_generals];
    uint8_t roster_active[2][max_battle_generals], roster_active_count[2];
    int8_t attacker_count, defender_count, attacker_index, defender_index, captured_count;
    int8_t army, opponent_army, city, winner, pending_winner;
    uint16_t tick, duel_tick, effect_ticks;
    // Presentation only: never feeds attack, movement or damage timers.
    uint16_t cast_pose_start_tick;
    uint16_t ai_decision_countdown[2];
    uint16_t effect_remaining;
    uint16_t heal_wait;
    uint16_t heal_remainder_wait;
    uint16_t heal_visual_remaining;
    uint16_t immunity_ticks[2];
    uint8_t effect_side, effect_kind, paused;
    uint8_t effect_applied, field_battle, terrain, row_setup_active;
    uint8_t heal_steps, heal_remainder;
    uint8_t magic_callback_damage;
    uint8_t round_result_kind, round_result_pending;
    uint8_t magic_field_iterations, magic_field_wait, magic_field_state;
    // demake's two ultimate skills are composition wrappers.  Keep the
    // currently running child outside the normal effect fields so each child
    // can reuse the existing GBA projectile/resource implementation.
    uint8_t magic_composite_kind, magic_composite_stage;
    uint8_t magic_composite_side, magic_composite_bootstrap;
    uint16_t magic_composite_wait;
    int16_t magic_field_center_x, magic_field_center_y;
    int16_t magic_field_origin_x, magic_field_origin_y;
    int16_t round_result_winner, round_result_loser;
    int16_t effect_script;
    int16_t camera_x, camera_y;
    uint8_t camera_manual, camera_y_manual;
    ActorVisual general_visual[2];
    ActorVisual soldier_visual[400];
    uint8_t general_hit_ticks[2];
    uint16_t general_hit_damage[2];
    uint8_t round_result_level_before;
    // Track spell-derived countdowns separately from ordinary scheduling so
    // issuing a new order cannot erase a transferred debuff. Transient only.
    uint8_t magic_delay_bits[(402+7)/8];
    bool magic_delay_active(int actor) const { return (magic_delay_bits[actor/8]>>(actor%8))&1; }
    void set_magic_delay_active(int actor,bool active) {
        const uint8_t bit=uint8_t(1u<<(actor%8));
        if(active) magic_delay_bits[actor/8]|=bit;
        else magic_delay_bits[actor/8]&=uint8_t(~bit);
    }
};
// Native 297's weighted-occupancy query; cell mapping/order remain approximate.
int native297_anchor_x(const Battle& battle,int side);
enum class BattleOfficerOutcome : uint8_t { stationed, marching, retreating, captured, scattered, rescued };
struct BattleReportEntry {
    int16_t general, city;
    int8_t king;
    BattleOfficerOutcome outcome;
};
struct BattleReport {
    BattleReportEntry entries[max_battle_generals];
    uint8_t count, captured_count, result, pending;
};
struct Game {
    Campaign campaign{};
    Battle battle{};
    BattleReport battle_report{};
    const char* notice = "";
    int16_t search_result_general = -1;
    int16_t search_result_city = -1;
    uint8_t search_result_joined = 0;
    void new_game(int scenario, int king);
    void advance_menu_random();
    int roll(int limit);
    int level(int g) const;
    uint32_t next_level_experience(int g) const;
    int strength(int g) const;
    int intelligence(int g) const;
    int max_hp(int g) const;
    int max_mp(int g) const;
    int max_soldiers(int g) const;
    int front_capacity(int g) const;
    int rear_capacity(int g) const;
    int skill_count(int g) const;
    int learned_skill(int g, int tier) const;
    int promotion_candidate_skill(int g,int tier,int candidate) const;
    void gain_experience(int g, int amount);
    void replenish(int g, int amount);
    int officers_at(int city, int king, int* out, int capacity) const;
    int neighbors(int city, int* out) const;
    bool route_reachable(int from,int target) const;
    uint64_t reachable_city_mask(int from) const;
    const char* domestic_block_reason(int city, int general, int action) const;
    bool domestic_action(int city, int general, int action);
    // Shared domestic eligibility: the ability thresholds live here so the menu
    // greying and the rule that actually executes cannot drift apart.
    bool domestic_qualifies(int general, int action) const;
    // demake bars the ruler from domestic work. That would be a permanent
    // soft-lock for a faction reduced to its ruler alone, because only search
    // can replace lost officers. Returns true in exactly that state.
    bool domestic_ruler_only() const;
    bool transfer_soldiers(int city, int general, int delta);
    bool appoint_major(int city, int general);
    bool appoint_skill(int general, int tier, int candidate);
    bool recruit(int city, int target);
    bool use_item(int general, int inventory_index);
    bool use_city_object(int general,int city,int inventory_index);
    bool send_army(int from, int to, const int* generals, int count, bool ai_attack=false, bool demake_ai=false, int requested_money=-1);
    bool redirect_army(int army,int target);
    uint32_t army_remaining_travel(int army_index) const;
    bool preview_army_redirect(int index,int target,Army& route,uint8_t& pending) const;
    bool resume_army(int army);
    int army_near_city(int army) const;
    bool garrison_army(int army);
    bool enter_army_city(int army);
    void finish_domestic(bool run_ai = true);
    void world_tick();
    void start_battle(int army);
    void start_field_battle(int first_army, int second_army);
    int resolve_demake_ai_field_battle(int first_army, int second_army);
    int resolve_ai_battle();
    void start_duel(bool confirmed = true);
    void battle_tick();
    bool issue_battle_order(int side,Order order);
    bool issue_battle_command(int side,BattleCommand command);
    bool cast_skill(int side, int slot);
    void resolve_duel(int winner);
    void finish_battle(int winner,bool native_auto = false);
    int battle_casualty_experience(int side, int roster_index) const;
    void add_battle_experience(int side, int roster_index, int amount);
    void distribute_battle_experience();
    void handle_prisoner(int general, bool persuade);
    bool persuade_prisoner(int general);
    const char* prisoner_persuasion_block(int general) const;
    void finish_prisoners();
    bool valid() const;
    uint32_t checksum() const;
private:
    bool cast_ai_skill(int side,int table_slot);
    void auto_cast_skill(int side);
    void auto_tactical_action(int side);
    friend void demake_general_ai_tick(Game& game,int side,bool controlled);
    bool cast_battle_skill(int side,const SkillDef* skill);
    bool start_magic_composite_child();
    void refresh_level_caps(int g);
    void check_level_up(int g);
    void ai_promote_general(int g);
    void resolve_demake_ai_city_battle(int army);
    void ai_tick();
    void ai_domestic_tick();
    // Original FUN_004032bb runs three level catch-ups per non-player faction
    // every year, in order: the independent faction experience ledger
    // (0x402d74), the player's high-EXP mean (0x402e66) and the player's
    // ten-best mean level (0x402f89). Only battle rewards feed the ledger.
    void ai_level_catchup();
    void advance_month();
    void arrive(int army);
    bool intercept_army_city(int army);
    void apply_fighter(int side);
    void apply_pending_effect();
    void apply_magic_general_callback(int script,int target_side,uint8_t mode);
    void update_fast_projectile(Projectile& p,const int16_t* cell_heads,const int16_t* cell_next);
    void update_magic_projectile_windup(Projectile& p);
    void hit_ghost_halberd_cell(Projectile& p,int head,const int16_t* cell_next,int column,int row);
    void update_fire_dragon_projectile(Projectile& p);
    void update_cyclone_projectile(Projectile& p);
    void update_firewheel_projectile(Projectile& p,const int16_t* cell_heads,const int16_t* cell_next);
    void update_battle_projectiles(const int16_t* cell_heads,const int16_t* cell_next);
    void update_magic_field();
    void spawn_m014_sparks(int x,int y);
    void casualties(int side, int number);
    int select_active_battle_general(int side);
    void check_end();
    void learn_skill(int g, int tier);
    uint32_t recruited_experience(int faction);
    void recovery_tick();
};
static_assert(sizeof(Campaign) < 16384, "Save must fit half SRAM for journal slots");
inline bool battle_view_mirrored(const Game& game) {
    return game.campaign.officers[game.battle.sides[0].general].king == game.campaign.player;
}
inline int battle_camera_pan_direction(const Game& game,int screen_direction) {
    return battle_view_mirrored(game) ? -screen_direction : screen_direction;
}
void demake_general_opening_tick(Game& game,int side);
void demake_general_ai_tick(Game& game,int side,bool controlled);
inline int battle_camera_max_x(const Game& game) {
    int rightmost = maximum(int(game.battle.sides[0].x), int(game.battle.sides[1].x));
    for(const auto& soldier : game.battle.soldiers) {
        if(soldier.hp > 0 && soldier.x > rightmost) rightmost = soldier.x;
    }
    return clamp(rightmost - 80, 0, battlefield_width - battle_view_width);
}
}
#endif
