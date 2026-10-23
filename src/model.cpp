#include "model.h"
#include "original_rules.h"
#include "formation_layouts.h"

namespace sg {

bool demake_battle_capture(Game& game,int loser_id,int winner_king,bool escaped);

namespace {
// Native FUN_0042033A: origin/LOYAL shortcuts precede the refusal gate.
// Return -1 for a living allegiance lock, 1 for automatic affiliation,
// and 0 for the ordinary persuasion path. Use scenario data, never names.
int prisoner_allegiance(const Campaign& campaign,int general,int captor) {
    if(native_origin_king(campaign,general)==captor) return 1;
    const int loyal=general_initial[campaign.scenario][general].loyal_king;
    if(loyal<0 || loyal>=scenario_defs[campaign.scenario].king_count) return 0;
    if(loyal==captor) return 1;
    const int ruler=king_defs[campaign.scenario][loyal].general;
    if(ruler<0 || ruler>=general_count) return 0;
    const auto& leader=campaign.officers[ruler];
    if(leader.king==loyal) return -1;
    if(leader.king==captor && leader.status==Status::employed) return 1;
    return 0;
}

// Exact (value * factor) / divisor in 32-bit arithmetic. A 64-bit expression
// here pulls __aeabi_uldivmod (over 3 KiB) into IWRAM, which the GBA stack
// reserve cannot spare, and the original is 32-bit anyway.
uint32_t scale_32(uint32_t value, uint32_t factor, uint32_t divisor) {
    if(!divisor) return 0;
    return (value / divisor) * factor + (value % divisor) * factor / divisor;
}
// A bounded GBA army pool must not become permanently exhausted by small AI
// garrisons outside full cities. Consolidate only co-located, same-faction AI
// garrisons when allocation actually runs out; never touch player formations
// or moving armies. All officers, captive links and money remain accounted for.
int available_army_slot(Campaign& campaign,int requesting_king) {
    // Unlike demake's growable list, GBA has a fixed pool. AI overflow at full
    // cities must not consume the last two player command slots indefinitely.
    if(requesting_king!=campaign.player) {
        int ai_count=0;
        for(const auto& army:campaign.armies)
            ai_count+=army.active && army.king!=campaign.player;
        if(ai_count>=max_armies-2) return -1;
    }
    for(int i=0;i<max_armies;++i) if(!campaign.armies[i].active) return i;
    for(int i=0;i<max_armies;++i) {
        auto& into=campaign.armies[i];
        if(!into.active || !into.garrisoned || into.king==campaign.player || into.count>=5) continue;
        int groups=1,members=into.count;
        for(int j=i+1;j<max_armies;++j) {
            const auto& other=campaign.armies[j];
            if(other.active && other.garrisoned && other.king==into.king && other.target==into.target) {
                ++groups; members+=other.count;
            }
        }
        // Do not partially rewrite armies if this group cannot free a slot.
        if(groups<2 || members>5*(groups-1)) continue;
        for(int j=i+1;j<max_armies && into.count<5;++j) {
            auto& from=campaign.armies[j];
            if(!from.active || !from.garrisoned || from.king!=into.king || from.target!=into.target) continue;
            while(from.count && into.count<5) {
                const int g=from.generals[--from.count];
                into.generals[into.count++]=int16_t(g);
                campaign.officers[g].army=int16_t(i);
            }
            if(from.count) continue;
            for(auto& officer:campaign.officers)
                if(officer.status==Status::prisoner && officer.army==j) officer.army=int16_t(i);
            into.money+=from.money;
            from.money=0; from.active=0; from.garrisoned=0;
            return j;
        }
    }
    return -1;
}

uint32_t demake_level_experience(int lv) {
    if(lv == 2) return 200;
    if(lv == 3) return 377;
    if(lv == 4) return 771;
    if(lv < 50) {
        lv -= 5;
        return uint32_t(1302 + 531 * lv + 137 * (lv + 1) * lv / 2);
    }
    return 99999;
}

int demake_magic_index(int skill_id) {
    for(int i=0;i<50;++i) if(demake_magic_skill_ids[i]==skill_id) return i;
    return -1;
}

int demake_magic_skill(int magic_index) {
    return magic_index>=0 && magic_index<50 ? demake_magic_skill_ids[magic_index] : 0;
}

const SkillDef* skill_by_script(int script) {
    for(const SkillDef& skill : skill_defs)
        if(skill.script==script) return &skill;
    return nullptr;
}

void demake_promotion_choices(int previous_magic,int job_level,int choices[3]) {
    int job_index=previous_magic,step=0;
    if(job_index<18) {
        job_index+=6;
        step=3;
        for(int i=0;i<3;++i) {
            choices[i]=job_index;
            job_index+=step;
            if(job_index>=job_level*6) job_index-=5;
        }
    } else {
        job_index+=10;
        step=5;
        for(int i=0;i<3;++i) {
            choices[i]=job_index;
            job_index+=step;
            if(job_index>=19+job_level*10) job_index-=9;
        }
    }
}

int native_angle_sine(int angle) {
    static constexpr int8_t quarter[65] = {
        0,2,5,7,9,12,14,16,19,21,23,26,28,30,32,35,
        37,39,41,43,45,47,49,51,53,55,57,59,61,63,64,66,
        68,70,71,73,74,76,77,78,80,81,82,84,85,86,87,88,
        89,90,90,91,92,93,93,94,94,95,95,95,96,96,96,96,96
    };
    angle &= 255;
    const int quadrant=angle>>6;
    const int offset=angle&63;
    if(quadrant==0) return quarter[offset];
    if(quadrant==1) return quarter[64-offset];
    if(quadrant==2) return -quarter[offset];
    return -quarter[64-offset];
}

}

int magic_particle_velocity(int speed,int azimuth,int elevation,int axis) {
    const int horizontal=native_angle_sine(64-elevation);
    const int sine_az=native_angle_sine(azimuth);
    const int cosine_az=native_angle_sine(azimuth+64);
    if(axis==0)
        return speed*horizontal*cosine_az*battle_grid_column_width*8/(96*96*96);
    if(axis==1)
        return speed*horizontal*sine_az*battle_grid_row_height*8/(96*96*72);
    return speed*native_angle_sine(elevation)*battle_grid_row_height*8/(96*72);
}

int magic_planar_particle_velocity(int speed,int azimuth,int axis) {
    const int direction=native_angle_sine(azimuth+(axis==0 ? 64 : 0));
    if(axis==0)
        return speed*direction*battle_grid_column_width*8/(96*96);
    if(axis==1)
        return speed*direction*battle_grid_row_height*8/(96*72);
    return 0;
}

int magic_native_polar_offset(int radius,int angle) {
    // Native 21 uses angle*pi/128 and converts the floating point offset to
    // an integer native coordinate. Q15 keeps that polar transform accurate
    // to less than one native pixel without a runtime floating point call.
    static constexpr uint16_t quarter[65] = {
        0,804,1608,2411,3212,4011,4808,5602,6393,7180,7962,8740,9512,
        10279,11039,11793,12540,13279,14010,14733,15447,16151,16846,
        17531,18205,18868,19520,20160,20788,21403,22006,22595,23170,
        23732,24279,24812,25330,25833,26320,26791,27246,27684,28106,
        28511,28899,29269,29622,29957,30274,30572,30853,31114,31357,
        31581,31786,31972,32138,32286,32413,32522,32610,32679,32729,
        32758,32768
    };
    angle &= 255;
    const int quadrant=angle>>6;
    const int offset=angle&63;
    int sine;
    if(quadrant==0) sine=quarter[offset];
    else if(quadrant==1) sine=quarter[64-offset];
    else if(quadrant==2) sine=-int(quarter[offset]);
    else sine=-int(quarter[64-offset]);
    return radius*sine/32768;
}

int magic_native_gravity_height_eighth(int vertical_speed,int age) {
    // native29(8192) reduces native vertical speed by 0.25 units after each
    // position update. Convert z = v*n - n*(n-1)/8 through the 72-to-30
    // battle-row scale and retain eighths of a GBA pixel for collision/render.
    const int n=maximum(0,age);
    return (40*vertical_speed*n-5*n*(n-1))/12;
}

int magic_native_vertical_velocity_q16(int speed,int elevation) {
    return speed*native_angle_sine(elevation)*65536/96;
}

int original::magic_general_callback_next_base(int script) {
    if(script == 2608 || script == 2611) return 2;
    if(script >= 2612 && script <= 2614) return 2;
    if(script == 2618 || script == 2622 || script == 2623) return 1;
    if(script == 2665) return 10; // demake: 20, then three 10-point hits.
    if((script >= 2600 && script <= 2605) ||
       (script >= 2630 && script <= 2635) || script == 2626 ||
       (script >= 2642 && script <= 2647) ||
       (script >= 2657 && script <= 2659) ||
       script == 2660 || script == 2663 || script == 2664) return 1;
    if(script >= 2639 && script <= 2641) return 2; // GLOBAL2521 changes 10 to 2.
    return 0;
}

// Recovered native297 weighted-occupancy max; cell mapping and native process
// ordering are still approximated by GBA world cells and live roster order.
int native297_anchor_x(const Battle& battle,int side) {
    if(side<0 || side>1) return battlefield_width/2;
    static constexpr int8_t weight[7][7] = {
        {1,2,3,4,3,2,1}, {2,3,4,5,4,3,2}, {3,4,5,6,5,4,3},
        {4,5,6,7,6,5,4}, {3,4,5,6,5,4,3}, {2,3,4,5,4,3,2},
        {1,2,3,4,3,2,1}
    };
    constexpr int origin=battlefield_width/2-28*battle_grid_column_width;
    int columns[200],rows[200],count=0;
    for(int i=0;i<200;++i) {
        const auto& soldier=battle.soldiers[side*200+i];
        if(soldier.hp<=0) continue;
        const int column=(int(soldier.x)-origin+battle_grid_column_width/2)/battle_grid_column_width;
        const int row=(int(soldier.y)+battle_grid_row_height/2)/battle_grid_row_height;
        if(column<0 || column>=battle_grid_columns || row<0 || row>=battle_grid_rows) continue;
        columns[count]=column;
        rows[count]=row;
        ++count;
    }
    int best=-1,best_score=-1;
    for(int candidate=0;candidate<count;++candidate) {
        int score=0;
        for(int ally=0;ally<count;++ally) {
            const int dx=columns[ally]-columns[candidate];
            const int dy=rows[ally]-rows[candidate];
            if(dx>=-3 && dx<=3 && dy>=-3 && dy<=3)
                score+=weight[dy+3][dx+3];
        }
        // Native297 replaces its candidate only for a strictly larger score.
        if(score>best_score) { best=candidate; best_score=score; }
    }
    if(best<0) return battle.sides[side].x;
    int seen=0;
    for(int i=0;i<200;++i) {
        const auto& soldier=battle.soldiers[side*200+i];
        if(soldier.hp<=0) continue;
        const int column=(int(soldier.x)-origin+battle_grid_column_width/2)/battle_grid_column_width;
        const int row=(int(soldier.y)+battle_grid_row_height/2)/battle_grid_row_height;
        if(column<0 || column>=battle_grid_columns || row<0 || row>=battle_grid_rows) continue;
        if(seen++==best) return soldier.x;
    }
    return battle.sides[side].x;
}

__attribute__((noinline)) void update_actor_visual(ActorVisual& visual,int dx,int dy,bool interpolating) {
    if(dx) visual.facing = uint8_t(dx > 0);
    if(dx || dy || interpolating) visual.motion_hold = 3;
    else if(visual.motion_hold) --visual.motion_hold;
    if(visual.motion_hold) {
        if(++visual.walk_tick >= 3) {
            visual.walk_tick = 0;
            visual.walk_frame = uint8_t((visual.walk_frame + 1) & 3);
        }
    } else {
        visual.walk_tick = 0;
        visual.walk_frame = 0;
    }
}
namespace {
// Native profile table at SANGO.exe 0x468050. It has five 16-int records;
// scenario KING TYPE is a direct 0..4 table index, including the hardest
// fifth record. Keep these rows in the original order.
constexpr int ai_profiles[5][16] = {
    {0,70,20000,7,2,6,2,40,60,70,50,2,20,30,50,400},
    {200,78,17000,6,2,6,2,45,70,75,60,3,30,30,40,500},
    {500,85,14000,5,2,6,2,50,80,85,75,4,35,30,35,600},
    {2000,90,11000,5,2,6,2,55,90,90,85,5,40,30,30,700},
    {4000,99,8000,4,2,6,2,60,100,100,100,6,60,30,10,800}
};

constexpr int formation_column_order[5] = {0,2,3,4,1};

int nearest_owned_city(const Campaign& campaign,int king,int start) {
    if(king < 0 || start < 0 || start >= city_count) return -1;
    int queue[city_count],distance[city_count];
    for(int city = 0; city < city_count; ++city) distance[city] = -1;
    int head = 0,tail = 0;
    queue[tail++] = start;
    distance[start] = 0;
    while(head < tail) {
        const int city = queue[head++];
        if(campaign.cities[city].king == king) return city;
        for(int next = 0; next < city_count; ++next) {
            if(distance[next] >= 0 || !((city_neighbors[city] >> next) & 1)) continue;
            distance[next] = distance[city] + 1;
            queue[tail++] = next;
        }
    }
    return -1;
}

int next_city_on_route(int start,int goal) {
    if(start < 0 || start >= city_count || goal < 0 || goal >= city_count || start == goal) return -1;
#ifndef SANGO_REFERENCE_WORLD_ROUTING
    if((city_neighbors[start]>>goal)&1) return goal;
#endif
    int queue[city_count],previous[city_count];
    for(int city = 0; city < city_count; ++city) previous[city] = -1;
    int head = 0,tail = 0;
    queue[tail++] = start;
    previous[start] = start;
    while(head < tail && previous[goal] < 0) {
        const int city = queue[head++];
        for(int next = 0; next < city_count; ++next) {
            if(previous[next] >= 0 || !((city_neighbors[city] >> next) & 1)) continue;
            previous[next] = city;
            queue[tail++] = next;
        }
    }
    if(previous[goal] < 0) return -1;
    int first = goal;
    while(previous[first] != start) first = previous[first];
    return first;
}

int city_path_length(int from,int to) {
    if(from<0 || from>=city_count || to<0 || to>=city_count) return -1;
    if(from==to) return 0;
#ifndef SANGO_REFERENCE_WORLD_ROUTING
    if((city_neighbors[from]>>to)&1) return city_route_lengths[from][to]==0xffff ? -1 : city_route_lengths[from][to];
#endif
    int length=0,current=from;
    for(int step=0;step<city_count && current!=to;++step) {
        const int next=next_city_on_route(current,to);
        if(next<0) return -1;
        const uint16_t edge_length=city_route_lengths[current][next];
        if(edge_length==0xffff) return -1;
        length+=edge_length;
        current=next;
    }
    return current==to ? length : -1;
}

uint16_t city_travel_ticks(int from,int to) {
    const int route_length=city_path_length(from,to);
    if(route_length<0) return 0xffff;
    // Keep the existing dispatch/arrival cadence while measuring the trip on
    // the imported original road network instead of a straight city chord.
    return uint16_t(minimum(0xffff,120+int(route_length)));
}

} // namespace

int map_route_cities(int from,int to,int* cities,int capacity) {
    if(!cities || capacity<1 || from<0 || from>=city_count || to<0 || to>=city_count) return 0;
    int count=0,current=from;
    cities[count++]=current;
    while(current!=to && count<capacity && count<city_count) {
        current=next_city_on_route(current,to);
        if(current<0) return 0;
        cities[count++]=current;
    }
    return current==to ? count : 0;
}

MapPoint army_route_position(const Army& army,uint32_t elapsed_ticks,bool reverse_geometry) {
    if(army.origin < 0 || army.origin >= city_count || army.target < 0 || army.target >= city_count)
        return {0,0};
    const uint32_t total_ticks = uint32_t(maximum(1,int(army.total)));
    const uint32_t progress = minimum(total_ticks,elapsed_ticks);
    const int route_length=city_path_length(army.origin,army.target);
    auto edge_position=[](int from_city,int to_city,int distance) -> MapPoint {
        const auto& route=city_routes[from_city][to_city];
        if(!route.count) return {city_defs[to_city].x,city_defs[to_city].y};
        if(route.count == 1) return {city_route_points[route.first].x,city_route_points[route.first].y};
        int segment = int(route.count)-1;
        for(int i = 1; i < route.count; ++i) {
            if(city_route_points[route.first+i].distance >= distance) {
                segment = i;
                break;
            }
        }
        const auto& from = city_route_points[route.first+segment-1];
        const auto& to = city_route_points[route.first+segment];
        const int segment_length = int(to.distance)-from.distance;
        if(segment_length <= 0) return {to.x,to.y};
        const int segment_progress = clamp(distance-int(from.distance),0,segment_length);
        return {
            int(from.x)+(int(to.x)-int(from.x))*segment_progress/segment_length,
            int(from.y)+(int(to.y)-int(from.y))*segment_progress/segment_length
        };
    };
    if(reverse_geometry) {
        const int length=city_route_lengths[army.target][army.origin];
        return edge_position(army.target,army.origin,length-int(uint32_t(length)*progress/total_ticks));
    }
    if(route_length>=0) {
        int distance=int(uint32_t(route_length)*progress/total_ticks);
        int current=army.origin;
        for(int step=0;step<city_count && current!=army.target;++step) {
            const int next=next_city_on_route(current,army.target);
            if(next<0) break;
            const int edge_length=city_route_lengths[current][next];
            if(edge_length==0xffff) break;
            if(distance<=edge_length) return edge_position(current,next,distance);
            distance-=edge_length;
            current=next;
        }
        return {city_defs[army.target].x,city_defs[army.target].y};
    }
    return {
        (int(city_defs[army.origin].x)*int(total_ticks-progress)+int(city_defs[army.target].x)*int(progress))/int(total_ticks),
        (int(city_defs[army.origin].y)*int(total_ticks-progress)+int(city_defs[army.target].y)*int(progress))/int(total_ticks)
    };
}

namespace {
void formation_split_counts(int layout, int amount, bool rear, int counts[5]) {
    for(int i = 0; i < 5; ++i) counts[i] = 0;
    if(amount <= 0 || layout < 0 || layout >= 14) return;
    if(rear) {
        int cumulative = formation_weights[layout][0];
        int previous = cumulative*amount/100;
        counts[0] = previous;
        for(int slot = 1; slot < 4; ++slot) {
            cumulative += formation_weights[layout][slot+1];
            const int current = cumulative*amount/100;
            counts[slot] = current-previous;
            previous = current;
        }
        counts[4] = amount-previous;
    } else {
        int cumulative = 0, previous = 0;
        for(int slot = 1; slot < 5; ++slot) {
            cumulative += formation_weights[layout][slot];
            const int current = cumulative*amount/100;
            counts[slot == 1 ? 4 : slot-1] = current-previous;
            previous = current;
        }
        counts[0] = amount-previous;
    }
}

FormationPoint formation_soldier_point(int layout, bool rear, int rank_index,
                                       int front_count, int rear_count,
                                       uint8_t* formation_column = nullptr) {
    int rear_counts[5], front_counts[5];
    formation_split_counts(layout,rear_count,true,rear_counts);
    formation_split_counts(layout,front_count,false,front_counts);
    int* counts = rear ? rear_counts : front_counts;
    int local_index = rank_index;
    int point_start = 0;
    for(int slot = 0; slot < 5; ++slot) {
        const int count = counts[slot];
        if(local_index < count) {
            if(!rear) local_index += rear_counts[slot];
            if(formation_column)
                *formation_column = uint8_t(formation_column_order[slot] + 1);
            return formation_points[layout][point_start+local_index];
        }
        local_index -= count;
        point_start += formation_weights[layout][formation_column_order[slot]];
    }
    return formation_points[layout][0];
}
}

int Game::roll(int limit) {
    if(limit <= 0) return 0;
    // FUN_00448202 native two-word rotate/add generator. The + operations
    // intentionally preserve the x86 behavior when a shift count is zero.
    const uint32_t next_primary = campaign.random + 1;
    const uint32_t previous_secondary = campaign.random_secondary - 1;
    const unsigned secondary_shift = next_primary & 31;
    campaign.random_secondary = (previous_secondary >> secondary_shift) +
        (previous_secondary << ((32-secondary_shift)&31));
    const unsigned primary_shift = campaign.random_secondary & 31;
    campaign.random = (next_primary << primary_shift) +
        (next_primary >> ((32-primary_shift)&31));
    return int((campaign.random + campaign.random_secondary) % uint32_t(limit));
}

void Game::advance_menu_random() {
    if(!campaign.random && !campaign.random_secondary) {
        campaign.random = original::random_initial_primary;
        campaign.random_secondary = original::random_initial_secondary;
    }
    // Native title/menu callback FUN_00437969 advances the process RNG once
    // each update. roll(1) consumes exactly that step, discarding its result.
    (void)roll(1);
}

int Game::level(int g) const { return campaign.officers[g].rank; }
uint32_t Game::next_level_experience(int g) const {
    return g>=0 && g<general_count && level(g)<50 ? demake_level_experience(level(g)+1) : 0;
}
int Game::strength(int g) const {
    return int(general_defs[g].str) + original::item_strength_bonus(campaign.officers[g].equipped_item);
}
int Game::intelligence(int g) const {
    return int(general_defs[g].intel) + original::item_intelligence_bonus(campaign.officers[g].equipped_item);
}
int Game::max_hp(int g) const {
    return minimum(999,int(officer_level_hp_cap(campaign.officers[g])) + original::item_max_hp_bonus(campaign.officers[g].equipped_item));
}
int Game::max_mp(int g) const {
    return minimum(999,int(officer_level_mp_cap(campaign.officers[g])) + original::item_max_mp_bonus(campaign.officers[g].equipped_item));
}
void Game::refresh_level_caps(int g) {
    auto& officer = campaign.officers[g];
    set_officer_level_hp_cap(officer,uint16_t(original::max_hp(general_defs[g].hp,strength(g),level(g))));
    set_officer_level_mp_cap(officer,uint16_t(original::max_mp(general_defs[g].mp,intelligence(g),level(g))));
}
int Game::front_capacity(int g) const { return original::front_capacity(level(g)); }
int Game::rear_capacity(int g) const { return original::rear_capacity(level(g)); }
int Game::max_soldiers(int g) const { return front_capacity(g)+rear_capacity(g); }
int Game::skill_count(int g) const { return original::skill_tiers(level(g)); }
int Game::learned_skill(int g,int tier) const { return tier >= 0 && tier < skill_count(g) ? campaign.officers[g].learned[tier] : 0; }
int Game::promotion_candidate_skill(int g,int tier,int candidate) const {
    if(g<0 || g>=general_count || tier<1 || tier>3 || candidate<0 || candidate>2) return 0;
    const auto& officer=campaign.officers[g];
    if(tier==3) {
        if(level(g)<40 || !officer.learned[2]) return 0;
        const int previous_magic=demake_magic_index(officer.learned[2]);
        return previous_magic<0 ? 0 : demake_magic_skill(previous_magic<18 ? 18 : 49);
    }
    const int previous_magic=demake_magic_index(officer.learned[tier==1 ? 0 : 1]);
    if(previous_magic<0) return 0;
    int choices[3];
    demake_promotion_choices(previous_magic,tier==1 ? 2 : 3,choices);
    return demake_magic_skill(choices[candidate]);
}

// 0x421407: a recruited officer's experience is recomputed from the recruiting
// faction's top third by EXP, scaled by 3/4 and a 90..119% roll. Both the
// player's search and the AI's own search attach through this routine.
uint32_t Game::recruited_experience(int faction) {
    int count = 0;
    for(int g = 0; g < general_count; ++g) {
        const auto& officer = campaign.officers[g];
        if(officer.status == Status::employed && officer.king == faction) ++count;
    }
    // Walk the distinct EXP values downwards instead of sorting a roster-sized
    // array: the GBA build has no room for another 255-entry buffer, and the
    // original only ever reads the mean of the best third.
    const int top_count = maximum(1, count / 3);
    uint32_t sum = 0, threshold = 0xffffffffu;
    int taken = 0;
    while(taken < top_count) {
        uint32_t best = 0; bool found = false;
        for(int g = 0; g < general_count; ++g) {
            const auto& officer = campaign.officers[g];
            if(officer.status != Status::employed || officer.king != faction) continue;
            const uint32_t exp = officer.exp;
            if(exp < threshold && (!found || exp > best)) { best = exp; found = true; }
        }
        if(!found) break;
        int equal = 0;
        for(int g = 0; g < general_count; ++g) {
            const auto& officer = campaign.officers[g];
            if(officer.status == Status::employed && officer.king == faction && officer.exp == best) ++equal;
        }
        const int take = minimum(equal, top_count - taken);
        sum += uint32_t(take) * best;
        taken += take;
        threshold = best;
    }
    const uint32_t average = taken ? sum / unsigned(taken) : 0;
    // 0x421407: avg EXP * 3, then integer 3/4, then a 90..119% roll.
    const uint32_t base = average * 3 / 4;
    return scale_32(base, unsigned(90 + roll(30)), 100);
}

void Game::learn_skill(int g,int tier) {
    auto& o = campaign.officers[g];
    const auto& d = general_defs[g];
    if(tier == 0) o.learned[0] = d.skills[0];
    else if(tier == 3) o.learned[3] = d.skills[7];
    else {
        int first = tier == 1 ? 1 : 4;
        bool king = o.king >= 0 && king_defs[campaign.scenario][o.king].general == g;
        int selected = king ? 0 : roll(3);
        for(int n = 0; n < 3 && d.skills[first+selected] == 0; ++n) selected = (selected+1)%3;
        o.learned[tier] = d.skills[first+selected];
    }
}

void Game::gain_experience(int g,int amount) {
    if(g < 0 || g >= general_count || amount <= 0) return;
    auto& o = campaign.officers[g];
    o.exp += unsigned(amount);
    check_level_up(g);
}

void Game::check_level_up(int g) {
    if(g < 0 || g >= general_count) return;
    auto& o=campaign.officers[g];
    while(o.rank < 50 && o.exp >= demake_level_experience(o.rank+1)) {
        int rear = o.soldiers-o.front;
        ++o.rank;
        int capacity = front_capacity(g);
        if(o.front > capacity) rear += o.front-capacity;
        o.front = uint16_t(minimum(int(o.front),capacity));
        rear = minimum(rear,rear_capacity(g));
        o.soldiers = uint16_t(o.front+rear);
        const int mp_bonus=original::item_max_mp_bonus(o.equipped_item);
        const int hp_bonus=original::item_max_hp_bonus(o.equipped_item);
        set_officer_level_mp_cap(o,uint16_t(clamp(int(officer_level_mp_cap(o))+1+roll(2),0,999-mp_bonus)));
        set_officer_level_hp_cap(o,uint16_t(clamp(int(officer_level_hp_cap(o))+1+roll(2),0,999-hp_bonus)));
        if(o.rank==50) o.exp=99999;
    }
}

void Game::ai_promote_general(int g) {
    if(g<0 || g>=general_count) return;
    auto& officer=campaign.officers[g];
    const int lv=level(g);
    if(lv<10) return;
    if(lv<30 && officer.learned[lv/10]!=0) return;
    if(lv>=40) {
        if(officer.learned[3]!=0) return;
        if(officer.learned[2]!=0) {
            const int previous_magic=demake_magic_index(officer.learned[2]);
            if(previous_magic<0) return;
            officer.learned[3]=uint8_t(demake_magic_skill(previous_magic<18 ? 18 : 49));
            return;
        }
    }

    bool top_job_level=false;
    while(!top_job_level) {
        int job_index=0,job_level=0;
        if(lv<20) {
            job_index=demake_magic_index(officer.learned[0]);
            job_level=2;
            top_job_level=true;
        } else if(officer.learned[1]==0) {
            job_level=2;
            job_index=demake_magic_index(officer.learned[0]);
        } else {
            job_level=3;
            job_index=demake_magic_index(officer.learned[1]);
            top_job_level=true;
        }
        if(job_index<0) return;

        officer.learned[job_level-1]=uint8_t(
            promotion_candidate_skill(g,job_level-1,roll(3)));
    }
}

void Game::replenish(int g,int amount) {
    auto& o = campaign.officers[g];
    amount = clamp(amount,0,max_soldiers(g)-int(o.soldiers));
    // demake's CityAct refills cavalry before infantry, just like the manual
    // conscription slider. `front` is the infantry pool in the native model.
    const int rear = int(o.soldiers)-int(o.front);
    const int cavalry_add = minimum(amount,rear_capacity(g)-rear);
    const int infantry_add = amount-cavalry_add;
    o.soldiers = uint16_t(o.soldiers+amount);
    o.front = uint16_t(o.front+infantry_add);
}

void Game::new_game(int scenario, int king) {
    uint32_t rng_primary = campaign.random;
    uint32_t rng_secondary = campaign.random_secondary;
    if(rng_primary == 0 && rng_secondary == 0) {
        // DAT_004850a0/a4 are initialized once with these values at process start.
        rng_primary = original::random_initial_primary;
        rng_secondary = original::random_initial_secondary;
    }
    campaign = {};
    battle = {};
    battle_report = {};
    search_result_general = -1;
    search_result_joined = 0;
    search_result_city = -1;
    scenario = clamp(scenario, 0, 4);
    king = clamp(king, 0, int(scenario_defs[scenario].king_count) - 1);
    campaign.random = rng_primary;
    campaign.random_secondary = rng_secondary;
    campaign.scenario = uint8_t(scenario);
    campaign.player = uint8_t(king);
    campaign.year = scenario_defs[scenario].year;
    campaign.month = 1;
    campaign.phase = Phase::domestic;
    for(int i = 0; i < city_count; ++i) {
        const auto& initial = city_initial[scenario][i];
        auto& city = campaign.cities[i];
        city.king = initial.king;
        city.major = initial.major;
        city.population = initial.population;
        city.defense = initial.defense;
        city.money = initial.money;
        if(city.major < 0) city.money = 0;
        city.reserve = 0;
        city.reserve_ticks = uint16_t(original::demake_reservist_interval);
    }
    for(int i = 0; i < general_count; ++i) {
        const auto& initial = general_initial[scenario][i];
        auto& officer = campaign.officers[i];
        officer.city = initial.city;
        officer.army = -1;
        officer.king = initial.king;
        officer.status = Status(initial.status);
        officer.exp = initial.exp;
        officer.rank = uint8_t(original::initial_level(initial.exp));
        refresh_level_caps(i);
        officer.hp = uint16_t(max_hp(i));
        officer.mp = uint16_t(max_mp(i));
        officer.soldiers = uint16_t(max_soldiers(i));
        officer.front = uint16_t(front_capacity(i));
        officer.force = initial.force;
        officer.forces = initial.forces;
        officer.formations = initial.formations;
        officer.formation = initial.formation;
        officer.loyalty = 80;
        native_origin_king(campaign,i) = initial.king < 0 ? 0xff : uint8_t(initial.king);
        native_status2_countdown(campaign,i) = general_defs[i].constancy;
        set_native_escape_count(campaign,i,0);
        for(int tier = 0; tier < skill_count(i); ++tier) {
            if(tier==0) officer.learned[0]=demake_initial_magic_skills[i];
            else learn_skill(i,tier);
        }
    }
    battle.winner = -1;
    notice = "每月进行内政，按 Start 进入大地图";
}

int Game::officers_at(int city, int king, int* out, int capacity) const {
    int count = 0;
    for(int i = 0; i < general_count && count < capacity; ++i) {
        const auto& o = campaign.officers[i];
        if(o.city == city && o.army < 0 && o.status == Status::employed && o.king == king) out[count++] = i;
    }
    return count;
}

int Game::neighbors(int city, int* out) const {
    if(city < 0 || city >= city_count) return 0;
    const int count = city_neighbor_counts[city];
    for(int i = 0; i < count; ++i) out[i] = city_neighbor_order[city][i];
    return count;
}

bool Game::route_reachable(int from,int target) const {
    return target>=0 && target<city_count && ((reachable_city_mask(from)>>target)&1);
}

uint64_t Game::reachable_city_mask(int from) const {
    if(from<0 || from>=city_count) return 0;
    int queue[city_count],head=0,tail=0;
    uint64_t visited=uint64_t(1)<<from;
    queue[tail++]=from;
    while(head<tail) {
        const int current=queue[head++];
        uint64_t candidates=city_neighbors[current]&~visited;
        for(int next=0;next<city_count;++next) {
            const uint64_t bit=uint64_t(1)<<next;
            if(!(candidates&bit) || city_route_lengths[current][next]==0xffff) continue;
            visited|=bit;
            queue[tail++]=next;
        }
    }
    return visited;
}

#if 0 // Replaced below by rules recovered from native SANGO.exe.
bool Game::domestic_action(int city_index, int g, int action) {
    if(campaign.phase != Phase::domestic || city_index < 0 || city_index >= city_count || g < 0 || g >= general_count) return false;
    auto& city = campaign.cities[city_index];
    auto& o = campaign.officers[g];
    if(city.king != campaign.player || o.king != campaign.player || o.city != city_index || o.army >= 0 || o.status != Status::employed) return false;
    if(o.worked) { notice = "姝ゆ灏囨湰骞村凡鍩疯鍏ф斂"; return false; }
    const int cost = action == 3 ? 100 : 150;
    if(city.money < cost) { notice = "閲戦將涓嶈冻"; return false; }
    if(action < 0 || action > 3) return false;
    city.money = uint16_t(city.money - cost);
    o.worked = 1;
    switch(action) {
    case 0:
        city.population += unsigned(general_defs[g].intel * 50);
        city.defense = uint16_t(minimum(999, int(city.defense) + general_defs[g].intel / 3));
        notice = "闁嬬櫦瀹屾垚 浜哄彛鑸囧煄闃叉彁鍗";
        break;
    case 1: {
        int candidates[general_count];
        int count = 0;
        for(int i = 0; i < general_count; ++i) {
            const auto& candidate = campaign.officers[i];
            if(candidate.status == Status::wild && candidate.city == city_index) candidates[count++] = i;
        }
        if(count && roll(100) < general_defs[g].intel) {
            auto& candidate = campaign.officers[candidates[roll(count)]];
            candidate.status = Status::employed;
            candidate.king = int8_t(campaign.player);
            candidate.loyalty = 70;
            notice = "鎼滅储鎴愬姛 鎷涘嫙浜嗕竴鍚嶅湪閲庢灏";
        } else notice = "鎼滅储鏈壘鍒伴鎰忓姞鍏ョ殑姝﹀皣";
        break;
    }
    case 2:
        city.reserve = uint16_t(minimum(999, int(city.reserve) + general_defs[g].str));
        for(int i = 0; i < general_count; ++i) {
            auto& candidate = campaign.officers[i];
            if(candidate.city == city_index && candidate.king == campaign.player && candidate.army < 0 && candidate.status == Status::employed) {
                int amount = minimum(int(city.reserve), max_soldiers(i) - candidate.soldiers);
                replenish(i,amount);
                city.reserve = uint16_t(city.reserve - amount);
            }
        }
        notice = "寰靛叺瀹屾垚 鍩庡収姝﹀皣宸茶鍏呭叺鍔";
        break;
    case 3:
        o.loyalty = uint8_t(minimum(100, int(o.loyalty) + 10));
        notice = "璩炶硿瀹屾垚 蹇犺獱鎻愬崌";
        break;
    default: break;
    }
    return true;
}
#endif

bool Game::domestic_qualifies(int g,int action) const {
    if(g < 0 || g >= general_count) return false;
    if(action == 0) return general_defs[g].intel >= 80;
    if(action == 1) return general_defs[g].str >= 80;
    return general_defs[g].str >= 85 || general_defs[g].intel >= 85;
}

bool Game::domestic_ruler_only() const {
    const int ruler = king_defs[campaign.scenario][campaign.player].general;
    for(int other = 0; other < general_count; ++other) {
        if(other == ruler) continue;
        const auto& o = campaign.officers[other];
        if(o.status == Status::employed && o.king == campaign.player) return false;
    }
    return true;
}

const char* Game::domestic_block_reason(int city_index,int g,int action) const {
    if(campaign.phase != Phase::domestic || city_index < 0 || city_index >= city_count || action < 0 || action > 2) return "内政指令无效";
    if(g < 0 || g >= general_count) return "本城没有经办武将";
    const auto& c=campaign.cities[city_index];
    const auto& o=campaign.officers[g];
    if(c.king != campaign.player || o.king != campaign.player || o.city != city_index || o.army >= 0 || o.status != Status::employed) return "只能由本城我方武将执行";
    if(king_defs[campaign.scenario][campaign.player].general == g && !domestic_ruler_only()) return "君主不能执行此项内政";
    if(o.worked) return "该武将本年已行动，明年恢复";
    if(action==0 && general_defs[g].intel<80) return "开发需要基础智力达到80";
    if(action==1 && general_defs[g].str<80) return "筑城需要基础武力达到80";
    if(action==2 && general_defs[g].str<85 && general_defs[g].intel<85) return "搜索需基础武力或智力达到85";
    return nullptr;
}

bool Game::domestic_action(int city_index, int g, int action) {
    if(const char* reason=domestic_block_reason(city_index,g,action)) { notice=reason; return false; }
    search_result_general = search_result_city = -1;
    search_result_joined = 0;
    auto& c = campaign.cities[city_index]; auto& o = campaign.officers[g];
    const int base_str = general_defs[g].str;
    const int base_int = general_defs[g].intel;
    if(action == 0) {
        const int delta=9000+roll(1000)+roll(maximum(1,(base_int-80)*100));
        c.population=uint32_t(minimum<uint64_t>(city_population_cap,
            uint64_t(c.population)+uint64_t(delta)));
        notice = "开发完成，人口增加";
    } else if(action == 1) {
        const int delta=30+roll(10)+roll(maximum(1,base_str-80));
        c.defense=uint16_t(minimum(city_defense_cap,int(c.defense)+delta));
        notice = "筑城完成，城防提高";
    } else {
        auto search_item = [&](int type,int limit) {
            if(city_inventory_count(campaign,city_index)>=city_search_inventory_limit) {
                notice="没有找到物品";
                return;
            }
            const int item=roll(limit);
            if(append_city_inventory_item(campaign,city_index,type,item))
                notice="搜索：发现物品，已存入本城物品栏";
            else notice="没有找到物品";
        };
        // SearchingController uses Random.Range(0,100)/20: five equal
        // outcome bands, with a separate 50/50 decision for a discovered
        // general. Objects are stored in the executing officer's city.
        switch(roll(100)/20) {
        case 0:
            notice="搜索：什么都没有发现";
            break;
        case 1: {
            int candidates[general_count],count=0;
            for(int i=0;i<general_count;++i)
                if(campaign.officers[i].king==-1) candidates[count++]=i;
            if(!count) {
                notice="搜索：没有找到人才";
                break;
            }
            const int target=candidates[roll(count)];
            auto& found=campaign.officers[target];
            search_result_general=int16_t(target);
            search_result_city=int16_t(city_index);
            if(roll(100)/50==0) {
                found.exp=recruited_experience(c.king);
                found.status=Status::employed;
                found.king=c.king;
                found.city=int16_t(city_index);
                found.army=-1;
                found.loyalty=90;
                found.worked=1;
                native_origin_king(campaign,target)=uint8_t(c.king);
                native_status2_countdown(campaign,target)=0;
                check_level_up(target);
                search_result_joined=1;
                notice="搜索到的人才加入了本城";
            } else notice="搜索发现的武将拒绝加入";
            break;
        }
        case 2: search_item(1,31); break;
        case 3: search_item(2,11); break;
        default: search_item(3,7); break;
        }
    }
    o.worked = 1;
    return true;
}

bool Game::transfer_soldiers(int city_index, int g, int delta) {
    if(city_index < 0 || city_index >= city_count || g < 0 || g >= general_count || campaign.cities[city_index].king != campaign.player) return false;
    auto& c = campaign.cities[city_index]; auto& o = campaign.officers[g];
    if(o.city != city_index || o.status != Status::employed || o.army >= 0 || o.king != campaign.player || !delta) return false;
    if(delta > 0) {
        // demake fills cavalry first, then infantry. The native model stores
        // infantry in `front` and cavalry in the rear pool.
        int amount = minimum(delta, int(c.reserve));
        amount = minimum(amount, max_soldiers(g) - int(o.soldiers));
        if(amount <= 0) return false;
        const int rear = int(o.soldiers) - int(o.front);
        const int cavalry_add = minimum(amount, rear_capacity(g) - rear);
        const int infantry_add = amount - cavalry_add;
        o.soldiers = uint16_t(o.soldiers + amount);
        o.front = uint16_t(o.front + infantry_add);
        c.reserve = uint16_t(c.reserve - amount);
        return true;
    }
    if(c.reserve >= original::demake_reserve_capacity(city_index)) return false;
    int amount = minimum(-delta, int(o.soldiers)); amount = minimum(amount, original::demake_reserve_capacity(city_index) - int(c.reserve));
    int front = minimum(amount, int(o.front)); o.front = uint16_t(o.front-front); o.soldiers = uint16_t(o.soldiers-amount); c.reserve = uint16_t(c.reserve+amount); return amount > 0;
}

bool Game::appoint_major(int city_index, int g) {
    if(city_index < 0 || city_index >= city_count || g < 0 || g >= general_count || campaign.cities[city_index].king != campaign.player) return false;
    int stationed[general_count], count = officers_at(city_index, campaign.player, stationed, general_count);
    bool present = false; for(int i = 0; i < count; ++i) present |= stationed[i] == g;
    if(!present) return false;
    campaign.cities[city_index].major = int16_t(g);
    return true;
}

bool Game::appoint_skill(int g, int tier, int candidate) {
    if(campaign.phase != Phase::domestic || g < 0 || g >= general_count ||
       campaign.officers[g].king != campaign.player || campaign.officers[g].status!=Status::employed) return false;
    auto& o = campaign.officers[g];
    if(tier == 3) {
        if(candidate != 0 || level(g) < 40 || !o.learned[2] || o.learned[3]) return false;
        o.learned[3] = uint8_t(promotion_candidate_skill(g,3,0));
        return o.learned[3] != 0;
    }
    if((tier != 1 && tier != 2) || candidate < 0 || candidate > 2 ||
       level(g) < (tier == 1 ? 10 : 20)) return false;
    if(o.learned[tier]) return false;
    if(tier == 2 && !o.learned[1]) return false;
    const int id=promotion_candidate_skill(g,tier,candidate);
    if(!id) return false;
    o.learned[tier] = uint8_t(id); return true;
}

bool Game::recruit(int city, int target) {
    if(city < 0 || city >= city_count || target < 0 || target >= general_count) return false;
    auto& o = campaign.officers[target];
    if(o.status != Status::prisoner || o.city != city || campaign.cities[city].king != campaign.player) return false;
    handle_prisoner(target, true);
    return o.status == Status::employed;
}

bool Game::use_item(int general, int inventory_index) {
    if((campaign.phase != Phase::domestic && campaign.phase != Phase::world) || general < 0 || general >= general_count) return false;
    auto& o = campaign.officers[general];
    if(o.king != campaign.player || o.status != Status::employed || o.city < 0 || o.army >= 0) return false;
    const int count=faction_inventory_count(campaign,o.king);
    if(inventory_index < 0 || inventory_index > count) return false;

    auto clamp_current_vitals = [&]() {
        o.hp = uint16_t(minimum(int(o.hp),max_hp(general)));
        o.mp = uint16_t(minimum(int(o.mp),max_mp(general)));
    };

    if(inventory_index == count) {
        if(!o.equipped_item) return false;
        const int old = o.equipped_item;
        if(!append_faction_item(campaign,o.king,old)) { notice="背包物品栏已满"; return false; }
        o.equipped_item = 0;
        clamp_current_vitals();
        notice = "已卸下装备";
        return true;
    }

    const int item = faction_inventory_item(campaign,o.king,inventory_index);
    if(item < 0 || item >= 55) return false;
    const int flag = item >= 43 && item <= 49 ? item-32 : item-1;
    if(((item >= 1 && item <= 11) || (item >= 43 && item <= 49)) &&
       (o.item_flags[flag >> 3] & (1 << (flag & 7)))) {
        notice = "该武将已使用过该物品";
        return false;
    }
    if(original::item_kinds[item] == 1) {
        const int force = original::item_force_type(item);
        const int formation = original::item_formation(item);
        if(force >= 0) o.forces |= uint16_t(1u << force);
        if(formation >= 0) o.formations |= uint8_t(1u << formation);
        o.item_flags[flag >> 3] |= uint8_t(1 << (flag & 7));
        remove_faction_item(campaign,o.king,item);
        notice = "已使用物品";
        return true;
    }

    if(o.equipped_item) {
        const int old = o.equipped_item;
        o.equipped_item = 0;
        clamp_current_vitals();
        o.equipped_item = uint8_t(item);
        remove_faction_item(campaign,o.king,item);
        set_faction_item_quantity(campaign,o.king,old,faction_item_quantity(campaign,o.king,old)+1);
    } else {
        o.equipped_item = uint8_t(item);
        remove_faction_item(campaign,o.king,item);
    }
    notice = "装备已更换";
    return true;
}


bool Game::use_city_object(int general,int city,int inventory_index) {
    if((campaign.phase!=Phase::domestic && campaign.phase!=Phase::world) ||
       general<0 || general>=general_count || city<0 || city>=city_count) return false;
    auto& officer=campaign.officers[general];
    if(officer.king<0 || officer.status!=Status::employed ||
       campaign.cities[city].king!=officer.king) return false;
    const int officer_city=officer.city>=0 && officer.city<city_count &&
        campaign.cities[officer.city].king==officer.king ? officer.city : city;
    auto change_equipment=[&](int new_item) {
        const int old_max_hp=max_hp(general),old_max_mp=max_mp(general);
        officer.equipped_item=uint8_t(new_item);
        const int new_max_hp=max_hp(general),new_max_mp=max_mp(general);
        officer.hp=uint16_t(clamp(int(officer.hp)+new_max_hp-old_max_hp,0,new_max_hp));
        officer.mp=uint16_t(clamp(int(officer.mp)+new_max_mp-old_max_mp,0,new_max_mp));
    };
    if(inventory_index==-1) {
        const int equipped=officer.equipped_item;
        if(!equipped) return false;
        if(equipped<12 || equipped>42 ||
           !append_city_inventory_item(campaign,officer_city,1,equipped-12)) {
            notice="城池物品栏已满";
            return false;
        }
        change_equipment(0);
        notice="已将装备放回所在城池";
        return true;
    }
    const int count=city_inventory_count(campaign,city);
    if(inventory_index<0 || inventory_index>=count) return false;
    const uint8_t entry=city_inventory_item(campaign,city,inventory_index);
    const int type=city_inventory_type(entry),index=city_inventory_index(entry);
    if(type==0) {
        const int item=index;
        if(item>=55) return false;
        const int flag=item>=43 && item<=49 ? item-32 : item-1;
        if(item>0 && ((item>=1 && item<=11) || (item>=43 && item<=49)) &&
           (officer.item_flags[flag>>3]&(1<<(flag&7)))) {
            notice="该武将已使用过该物品";
            return false;
        }
        if(original::item_kinds[item]==1) {
            const int force=original::item_force_type(item),formation=original::item_formation(item);
            if(force>=0) officer.forces|=uint16_t(1u<<force);
            if(formation>=0) officer.formations|=uint8_t(1u<<formation);
            if(item>0) officer.item_flags[flag>>3]|=uint8_t(1<<(flag&7));
            remove_city_inventory_item(campaign,city,inventory_index);
            notice="已使用物品";
            return true;
        }
        if(officer.equipped_item) {
            const int old=officer.equipped_item;
            const uint8_t old_entry=legacy_item_city_object(old);
            if(!old_entry) {
                notice="所在城池物品栏已满，无法替换装备";
                return false;
            }
            if(!replace_city_inventory_item(campaign,city,inventory_index,
                                            city_inventory_type(old_entry),city_inventory_index(old_entry))) return false;
        } else remove_city_inventory_item(campaign,city,inventory_index);
        change_equipment(item);
        notice="装备已更换";
        return true;
    }
    if(type==1) {
        const int item=12+index;
        if(index>=31) return false;
        if(officer.equipped_item) {
            const int old=officer.equipped_item;
            if(old<12 || old>42) {
                notice="所在城池物品栏已满，无法替换装备";
                return false;
            }
            if(!replace_city_inventory_item(campaign,city,inventory_index,1,old-12)) return false;
        } else remove_city_inventory_item(campaign,city,inventory_index);
        change_equipment(item);
        notice="装备已穿戴";
        return true;
    }
    if(type==2) {
        if(index>=11) return false;
        const int force=original::item_force_type(index+1);
        if(force<0 || (officer.forces&(1u<<force))) {
            notice="该兵种已掌握";
            return false;
        }
        officer.forces|=uint16_t(1u<<force);
        remove_city_inventory_item(campaign,city,inventory_index);
        notice="已习得新的兵种";
        return true;
    }
    if(type==3) {
        if(index>=7 || (officer.formations&(1u<<index))) {
            notice="该阵形已掌握";
            return false;
        }
        officer.formations|=uint8_t(1u<<index);
        remove_city_inventory_item(campaign,city,inventory_index);
        notice="已习得新阵形";
        return true;
    }
    return false;
}

bool Game::send_army(int from, int to, const int* generals, int count, bool ai_attack, bool demake_ai, int requested_money) {
    if(campaign.phase != Phase::world) { notice = "请先结束内政再出征"; return false; }
    if(from < 0 || from >= city_count || to < 0 || to >= city_count || count < 1 || count > 5) { notice = "出征信息无效，请重新编队"; return false; }
    const bool waiting_for_target=from==to;
    if(!waiting_for_target && !((city_neighbors[from] >> to) & 1)) { notice = "只能向道路相连的城池出征"; return false; }
    int king = campaign.cities[from].king;
    if(king < 0) { notice = "出发城池没有所属势力"; return false; }
    for(int i = 0; i < count; ++i) {
        int g = generals[i];
        if(g < 0 || g >= general_count) { notice = "出征名单中有无效武将"; return false; }
        const auto& o = campaign.officers[g];
        if(o.king != king || o.city != from || o.army >= 0 || o.status != Status::employed || o.hp == 0) { notice = "所选武将已离城或无法出征"; return false; }
        for(int j = 0; j < i; ++j) if(generals[j] == g) { notice = "出征名单不能重复选择武将"; return false; }
    }
    const int free_army=available_army_slot(campaign,king);
    if(free_army < 0) { notice = "行军部队已达上限"; return false; }
    auto& a = campaign.armies[free_army];
    a = {};
    army_pending_destination(campaign,free_army)=0;
    a.count = uint8_t(count);
    a.king = int8_t(king);
    a.origin = int8_t(from);
    a.target = int8_t(to);
    a.retreat_goal = -1;
    a.total = waiting_for_target ? 0 : city_travel_ticks(from,to);
    a.remaining = a.total;
    a.garrisoned = uint8_t(waiting_for_target);
    a.active = 1;
    auto& source = campaign.cities[from];
    if(requested_money >= 0) a.money = uint32_t(minimum(requested_money,int(source.money)));
    else if(demake_ai) a.money = source.money ? uint32_t(roll(int(source.money))) : 0;
    else {
        const int fund_percent = king == campaign.player || ai_attack ? 10 + roll(30) : 5 + roll(10);
        a.money = source.money * uint32_t(fund_percent) / 100u;
    }
    source.money -= a.money;
    for(int i = 0; i < count; ++i) {
        a.generals[i] = int16_t(generals[i]);
        auto& o = campaign.officers[generals[i]];
        o.army = int16_t(free_army);
        o.city = -1;
    }
    bool has_prisoners = false;
    for(const auto& officer : campaign.officers)
        has_prisoners |= officer.status == Status::prisoner && officer.king == king && officer.city == from && officer.army < 0;
    if(demake_ai && has_prisoners && roll(100) > 60) {
        for(auto& officer : campaign.officers) {
            if(officer.status != Status::prisoner || officer.king != king || officer.city != from || officer.army >= 0) continue;
            officer.city = -1;
            officer.army = int16_t(free_army);
        }
    }
    bool major_remains = false;
    for(int i = 0; i < count; ++i) major_remains |= generals[i] == source.major;
    if(major_remains) {
        int remaining[general_count], remaining_count = officers_at(from,king,remaining,general_count);
        if(demake_ai && remaining_count) {
            int replacement=remaining[0];
            const int ruler=king_defs[campaign.scenario][king].general;
            for(int i=0;i<remaining_count;++i) {
                if(remaining[i]==ruler) { replacement=ruler; break; }
                if(strength(remaining[i])>strength(replacement)) replacement=remaining[i];
            }
            source.major=int16_t(replacement);
        } else source.major = remaining_count ? int16_t(remaining[0]) : -1;
    }
    if(king == campaign.player) notice = "部队已出征";
    return true;
}

bool Game::redirect_army(int army_index,int target) {
    Army route;uint8_t pending;
    if(!preview_army_redirect(army_index,target,route,pending)) return false;
    campaign.armies[army_index]=route;
    army_pending_destination(campaign,army_index)=pending;
    return true;
}

uint32_t Game::army_remaining_travel(int army_index) const {
    if(army_index<0 || army_index>=max_armies || !campaign.armies[army_index].active) return 0;
    const auto& army=campaign.armies[army_index];
    const int goal=army_destination(campaign,army_index);
    const uint16_t tail=goal==army.target ? 0 : city_travel_ticks(army.target,goal);
    return uint32_t(army.remaining)+(tail==0xffff ? 0 : tail);
}

bool Game::preview_army_redirect(int army_index,int target,Army& army,uint8_t& pending) const {
    if(army_index<0 || army_index>=max_armies || target<0 || target>=city_count) return false;
    army=campaign.armies[army_index];
    pending=faction_storage_byte(campaign,army_destination_storage+army_index);
    if(!army.active || army.king!=campaign.player) return false;
    // Reaching an enemy gate commits the encounter. A menu opened in the
    // one-tick arrival handoff must not rebuild a route from inside that city.
    if(army.total && !army.remaining && !army.retreating &&
       campaign.cities[army.target].king>=0 && campaign.cities[army.target].king!=army.king) return false;
    if(target==army_destination(campaign,army_index)) {
        // demake accepts clicking the existing goal and resumes Running. Keep
        // live travel progress; an army already at this city resolves arrival
        // on the next world update, including a freshly assembled expedition.
        army.retreating=0; army.retreat_goal=-1; army.garrisoned=0;
        return true;
    }
    if(army.total && target==army.origin && ((city_neighbors[army.origin]>>army.target)&1)) {
        pending=army_reverse_geometry(campaign,army_index) ? 0 : 0x80;
        const int total=maximum(1,int(army.total));
        const int progress=total-minimum(total,int(army.remaining));
        if(!progress) {
            pending=0;
            army.target=army.origin;
            army.total=army.remaining=0;
            army.retreating=0; army.retreat_goal=-1; army.garrisoned=0;
            return true;
        }
        // demake rebuilds the route from the army's live position. Reversing
        // the stored edge and using elapsed ticks as remaining preserves that
        // position exactly in the GBA's city-edge travel representation.
        const int old_origin=army.origin;
        army.origin=army.target;
        army.target=int8_t(old_origin);
        army.total=uint16_t(total);
        army.remaining=uint16_t(progress);
        army.retreating=0; army.retreat_goal=-1; army.garrisoned=0;
        return true;
    }
    // Locate the live point on the actual road, not the nearer city. A new
    // path can leave this edge through either endpoint without teleporting.
    const bool reversed=army_reverse_geometry(campaign,army_index);
    const int length=reversed ? city_route_lengths[army.target][army.origin] : city_path_length(army.origin,army.target);
    if(length<0) return false;
    int distance=int(uint32_t(length)*uint32_t(army.total-minimum(army.total,army.remaining))/uint32_t(maximum(1,int(army.total))));
    int current=army.origin;
    if(army.total && army.remaining && distance>0) {
        for(int step=0;step<city_count && current!=army.target;++step) {
            const int next=next_city_on_route(current,army.target);
            if(next<0) return false;
            const int edge=reversed ? length : city_route_lengths[current][next];
            if(distance>edge) { distance-=edge; current=next; continue; }
            const int forward=city_path_length(next,target), backward=city_path_length(current,target);
            if(forward<0 && backward<0) return false;
            const bool whole_edge=current==army.origin && next==army.target;
            const int edge_ticks=whole_edge ? int(army.total) : maximum(1,(edge*int(army.total)+length/2)/maximum(1,length));
            const int forward_ticks=whole_edge ? int(army.remaining) : (edge-distance)*edge_ticks/maximum(1,edge);
            army.origin=int8_t(current); army.target=int8_t(next);
            army.total=uint16_t(edge_ticks); army.remaining=uint16_t(forward_ticks);
            const bool reverse=backward>=0 && (forward<0 || distance+backward<edge-distance+forward);
            if(reverse) {
                army.origin=int8_t(next); army.target=int8_t(current);
                army.remaining=uint16_t(edge_ticks-forward_ticks);
            }
            pending=uint8_t((target+1) | ((reversed!=reverse) ? 0x80 : 0));
            army.retreating=0; army.retreat_goal=-1; army.garrisoned=0;
            return true;
        }
    } else if(army.total && !army.remaining) current=army.target;
    const uint16_t ticks=city_travel_ticks(current,target);
    if(ticks==0xffff) return false;
    pending=0;
    army.origin=int8_t(current); army.target=int8_t(target);
    army.total=ticks; army.remaining=ticks;
    army.retreating=0; army.retreat_goal=-1; army.garrisoned=0;
    return true;
}

bool Game::garrison_army(int army_index) {
    if(army_index<0 || army_index>=max_armies) return false;
    auto& army=campaign.armies[army_index];
    if(!army.active || army.king!=campaign.player) return false;
    army.retreating=0; army.retreat_goal=-1; army.garrisoned=1;
    return true;
}

bool Game::resume_army(int army_index) {
    if(army_index<0 || army_index>=max_armies) return false;
    auto& army=campaign.armies[army_index];
    if(!army.active || army.king!=campaign.player || !army.garrisoned) return false;
    if(army.origin==army.target && !army.remaining) return false;
    army.garrisoned=0; army.retreating=0; army.retreat_goal=-1;
    return true;
}

int Game::army_near_city(int army_index) const {
    if(army_index<0 || army_index>=max_armies || !campaign.armies[army_index].active) return -1;
    const auto p=army_position(campaign,army_index);
    int nearest=-1,best=26;
    for(int c=0;c<city_count;++c) {
        const int dx=p.x-city_defs[c].x,dy=p.y-city_defs[c].y;
        const int d=dx*dx+dy*dy;
        if(d<best) { best=d;nearest=c; }
    }
    return nearest;
}

bool Game::enter_army_city(int army_index) {
    if(army_index<0 || army_index>=max_armies) return false;
    auto& army=campaign.armies[army_index];
    if(!army.active || army.king!=campaign.player) return false;
    const int current=army_near_city(army_index);
    if(current<0 || current>=city_count || campaign.cities[current].king!=army.king) return false;
    army.origin=int8_t(current); army.target=int8_t(current);
    army_pending_destination(campaign,army_index)=0;
    army.total=army.remaining=0;
    army.retreating=0; army.retreat_goal=-1;
    army.garrisoned=0;
    arrive(army_index);
    if(army.active && army.remaining==0) army.garrisoned=1;
    return true;
}

// Native per-faction level catch-up, run once per game year. The three passes
// mirror FUN_004032bb's order and its independent experience ledger: only
// battle rewards reach faction_experience, while every catch-up grant raises an
// officer's own EXP through gain_experience and never touches the ledger.
//
// The original sorts the player roster into a list. Both references it feeds
// only ever need the ten best officers, so this keeps a sorted ten-entry top
// list instead: no per-call array large enough for GBA to promote into IWRAM.
void Game::ai_level_catchup() {
    if(campaign.player >= max_kings) return;
    constexpr int top_cap = 10;
    uint32_t top_exp[top_cap];
    int top_level[top_cap];
    int top_count = 0, player_count = 0;
    for(int g = 0; g < general_count; ++g) {
        const auto& o = campaign.officers[g];
        if(o.status != Status::employed || o.king != campaign.player) continue;
        ++player_count;
        const uint32_t exp = o.exp;
        if(top_count == top_cap && exp <= top_exp[top_cap - 1]) continue;
        int at = minimum(top_count, top_cap - 1);
        while(at > 0 && top_exp[at - 1] < exp) {
            top_exp[at] = top_exp[at - 1];
            top_level[at] = top_level[at - 1];
            --at;
        }
        top_exp[at] = exp;
        top_level[at] = level(g);
        if(top_count < top_cap) ++top_count;
    }
    if(!player_count || !top_count) return;
    // 0x402e66: mean EXP of the player's best min(10,max(1,P/2)) officers.
    const int mean_count = minimum(top_cap, maximum(1, player_count / 2));
    uint32_t mean_total = 0;
    for(int i = 0; i < mean_count; ++i) mean_total += top_exp[i];
    const uint32_t mean_exp = mean_total / unsigned(mean_count);
    // 0x402f89: mean level of the player's ten best.
    const int rank_count = minimum(top_cap, player_count);
    uint32_t rank_total = 0;
    for(int i = 0; i < rank_count; ++i) rank_total += unsigned(top_level[i]);
    const int mean_level = int(rank_total / unsigned(rank_count));
    // D = threshold(L) - threshold(L-1). At L<=1 the original subtraction goes
    // negative, which the grant path then discards; keep that effect explicit.
    const int rank_step = demake_level_experience(mean_level) -
        (mean_level > 0 ? demake_level_experience(mean_level - 1) : 0);

    const int kings = scenario_defs[campaign.scenario].king_count;
    for(int king = 0; king < kings; ++king) {
        if(king == campaign.player || king < 0 || king >= max_kings) continue;
        int faction_count = 0;
        for(int g = 0; g < general_count; ++g) {
            const auto& o = campaign.officers[g];
            if(o.status == Status::employed && o.king == king) ++faction_count;
        }
        if(!faction_count) continue;
        // 0x402d74: normalise the player's ledger by both employed counts, then
        // apply a 100..134% roll. Only a faction behind its target is topped up.
        const uint32_t base = scale_32(campaign.faction_experience[campaign.player],
            unsigned(faction_count), unsigned(player_count));
        const uint32_t target = scale_32(base, unsigned(100 + roll(0x23)), 100);
        if(campaign.faction_experience[king] < target) {
            const uint32_t per_officer = (target - campaign.faction_experience[king]) / unsigned(faction_count);
            const uint32_t grant = scale_32(per_officer, unsigned(0x50 + roll(0x28)), 100);
            if(grant) for(int g = 0; g < general_count; ++g) {
                const auto& o = campaign.officers[g];
                if(o.status == Status::employed && o.king == king) gain_experience(g, int(grant));
            }
        }
        // 0x402e66: close half the gap to the player's high-EXP mean. No roll.
        for(int g = 0; g < general_count; ++g) {
            const auto& o = campaign.officers[g];
            if(o.status != Status::employed || o.king != king) continue;
            if(o.exp < mean_exp) gain_experience(g, int((mean_exp - o.exp) / 2));
        }
        // 0x402f89: FUN_00401000 gate -- needs eleven or more player officers,
        // then allows at most four levels of window.
        const int rank_window = player_count >= 11 ? minimum(4, player_count / faction_count) : 0;
        if(rank_window > 0 && rank_step > 0) for(int g = 0; g < general_count; ++g) {
            const auto& o = campaign.officers[g];
            if(o.status != Status::employed || o.king != king) continue;
            const int current_level = level(g);
            if(current_level >= mean_level + rank_window) continue;
            const int grant = current_level < mean_level ? rank_step :
                (current_level < 31 || current_level <= mean_level + 1) ? rank_step / 2 : 0;
            if(grant) gain_experience(g, grant);
        }
    }
}

void Game::advance_month() {
    ++campaign.month;
    if(campaign.month > 12) {
        campaign.month = 1;
        ++campaign.year;
        // demake OnTimeOverMode excludes only unowned/neutral cities.
        // Informations.kingNum is the neutral sentinel, not Controller.kingIndex.
        for(auto& city : campaign.cities)
            if(city.king >= 0) {
                city.money += original::demake_city_annual_income(city.population);
                if(city.money > 999999) city.money = 999999;
            }
        campaign.phase = Phase::domestic;
        // FUN_004032bb's yearly sweep. This replaces a flat "half a level for
        // every AI officer", which let the AI out-level a passive player and
        // never fall behind an active one.
        ai_level_catchup();
        for(auto& o : campaign.officers) o.worked = 0;
        notice = "新年度内政阶段，按 Start 继续";
    } else {
        // demake MonthAct recovers generals after ordinary months, but not on
        // the year rollover. Recovery is based on STR/INT rather than daily ticks.
        for(int i = 0; i < general_count; ++i) {
            auto& o = campaign.officers[i];
            o.hp = uint16_t(minimum(max_hp(i), int(o.hp) + strength(i) / 20));
            o.mp = uint16_t(minimum(max_mp(i), int(o.mp) + intelligence(i) / 20));
        }
        auto demake_move = [&](int from,int to,int requested) {
            while(requested > 0) {
                const int batch = minimum(requested,5);
                int roster[general_count], roster_count = officers_at(from,campaign.cities[from].king,roster,general_count);
                int selected[5], selected_count = 0;
                int prefect_commander = -1;
                for(int j = roster_count-1; j >= 0 && selected_count < batch; --j) {
                    const int g = roster[j];
                    if(campaign.officers[g].hp <= 50) continue;
                    if(g == campaign.cities[from].major) {
                        if(roll(100) <= 50) continue;
                        prefect_commander = g;
                    }
                    selected[selected_count++] = g;
                }
                if(!selected_count) return;
                // demake chooses the ruler if selected, otherwise the strongest
                // available general; put that commander first in the GBA roster.
                int commander = prefect_commander;
                if(commander < 0) {
                    commander = selected[0];
                    bool ruler_selected = false;
                    for(int i = 0; i < selected_count; ++i)
                        if(selected[i] == king_defs[campaign.scenario][campaign.cities[from].king].general) {
                            commander = selected[i]; ruler_selected = true; break;
                        }
                    if(!ruler_selected) for(int i = 1; i < selected_count; ++i)
                        if(strength(selected[i]) > strength(commander)) commander = selected[i];
                    }
                for(int i = 0; i < selected_count; ++i) if(selected[i] == commander) {
                    const int temp = selected[0]; selected[0] = selected[i]; selected[i] = temp; break;
                }
                if(!send_army(from,to,selected,selected_count,false,true)) return;
                requested -= 5;
            }
        };
        // demake CityAct fills stationed AI soldiers from the city's reserve,
        // in roster order, before it makes that month's army decision.
        for(int city_index = 0; city_index < city_count; ++city_index) {
            auto& city = campaign.cities[city_index];
            if(city.king < 0 || city.king == campaign.player) continue;
            int roster[general_count];
            const int count = officers_at(city_index,city.king,roster,general_count);
            for(int n = 0; n < count && city.reserve > 0; ++n) {
                auto& officer = campaign.officers[roster[n]];
                const int missing = maximum(0,max_soldiers(roster[n])-int(officer.soldiers));
                const int added = minimum(missing,int(city.reserve));
                replenish(roster[n],added);
                city.reserve = uint16_t(int(city.reserve)-added);
            }

            // Port demake's per-neighbor CityAct thresholds and requested
            // roster sizes; the first qualifying neighbor ends this month's
            // decision for the city.
            int ready = 0;
            for(int n = 0; n < count; ++n) {
                const auto& officer = campaign.officers[roster[n]];
                if(officer.hp > 50 && int(officer.soldiers)*2 > max_soldiers(roster[n])) ++ready;
            }
            int around[city_count], around_count = neighbors(city_index,around);
            for(int n = 0; n < around_count; ++n) {
                const int target = around[n];
                const auto& neighbor = campaign.cities[target];
                int defenders[general_count];
                const int defender_count = officers_at(target,neighbor.king,defenders,general_count);
                if(neighbor.king == city.king) {
                    if(ready < 8 && defender_count > 2 && roll(100) > 88) {
                        demake_move(target,city_index,1+roll(defender_count-1));
                        break;
                    }
                    continue;
                }
                if(neighbor.king < 0) {
                    if(ready > 3) {
                        const int attack_roll = roll(100);
                        if(attack_roll > 80) {
                            demake_move(city_index,target,1+roll(maximum(1,count-1)));
                            break;
                        }
                    }
                    continue;
                }
                if(ready <= 2 || roll(100) <= 88) continue;
                const int difference = ready-defender_count;
                if(difference >= 2) {
                    demake_move(city_index,target,defender_count+1+roll(maximum(1,count-defender_count-1)));
                    break;
                }
                if(difference > 0) {
                    if(roll(100) > 50) demake_move(city_index,target,defender_count);
                    break;
                }
                if(difference == 0 && roll(100) > 88)
                    demake_move(city_index,target,defender_count-1);
                if(difference == 0) break;
            }
        }

        // demake ArmyAct follows CityAct each ordinary month.  A non-player
        // army left in Garrison is checked at its city position and re-enters
        // through the same capacity-limited IntoTheCity path used on arrival.
        // Player armies remain under explicit map commands.
        int ai_army_count=0;
        for(const auto& army:campaign.armies)
            ai_army_count+=army.active && army.king!=campaign.player;
        for(int army_index = 0; army_index < max_armies; ++army_index) {
            auto& army = campaign.armies[army_index];
            if(!army.active || !army.garrisoned || army.king == campaign.player) continue;
            const int city = army.target;
            if(city < 0 || city >= city_count || campaign.cities[city].king != army.king) continue;
            arrive(army_index);
            // Recover old saves already above the new AI budget without
            // deleting officers/cargo or teleporting a garrison into a full
            // city. Overflow may march to a neighboring friendly vacancy or
            // an undefended city, then settle through ordinary arrival.
            if(ai_army_count>max_armies-2 && army.active && army.garrisoned) {
                int adjacent[city_count]; const int count=neighbors(city,adjacent);
                int target=-1,best=3;
                for(int n=0;n<count;++n) {
                    const int next=adjacent[n],king=campaign.cities[next].king;
                    int stationed[general_count];
                    const int population=officers_at(next,king,stationed,general_count);
                    const int score=king==army.king && population+army.count<=10 ? 0 :
                        king!=army.king && population==0 ? 1 : 3;
                    if(score<best) { best=score; target=next; }
                }
                if(target>=0) {
                    const uint16_t travel=city_travel_ticks(city,target);
                    if(travel!=0xffff) {
                        army.origin=int8_t(city); army.target=int8_t(target);
                        army.total=army.remaining=travel;
                        army.garrisoned=0; army.retreating=0; army.retreat_goal=-1;
                    }
                }
            }
        }
    }
    for(int i = 0; i < general_count; ++i) {
        auto& o = campaign.officers[i];
        // POS gives historical locations for all five eras; spawn at the next era's start year.
        if(o.status == Status::absent) {
            int era = campaign.scenario;
            for(int n = campaign.scenario + 1; n < scenario_count; ++n) if(campaign.year >= scenario_defs[n].year) era = n;
            const auto& initial = general_initial[era][i];
            if(initial.status != 0 && initial.city >= 0) { o.city = initial.city; o.status = Status::wild; }
        }
    }
    // Native FUN_00417c72 leaves the campaign open through the first five
    // months of year 280, then enters the timed-ending branch in month six.
    if(campaign.year >= 280 && campaign.month >= 6) {
        campaign.phase = Phase::ended;
        notice = "280年司马炎统一全中国";
    }
}

void Game::recovery_tick() {
    for(int city_index = 0; city_index < city_count; ++city_index) {
        auto& c = campaign.cities[city_index];
        if(c.reserve_ticks > 1) --c.reserve_ticks;
        else {
            c.reserve_ticks = uint16_t(original::demake_reservist_interval);
            // kingNum is demake's neutral sentinel. Player cities must replenish
            // too: otherwise casualties cannot be replaced in a long campaign.
            if(c.king >= 0 && c.money > 100 &&
               c.reserve < original::demake_reserve_capacity(city_index)) {
                ++c.reserve;
                c.money -= 100;
            }
        }
    }
    for(int i = 0; i < general_count; ++i) {
        auto& o = campaign.officers[i];
        // Native officer tick clamps HP/MP and the front/rear soldier pools
        // even for officers who are not currently recovering in a city.
        o.hp = uint16_t(clamp(int(o.hp),1,max_hp(i)));
        o.mp = uint16_t(clamp(int(o.mp),1,max_mp(i)));
        int front = minimum(int(o.front),front_capacity(i));
        int rear = minimum(maximum(0,int(o.soldiers)-int(o.front)),rear_capacity(i));
        o.front = uint16_t(front);
        o.soldiers = uint16_t(front+rear);
    }
}

void Game::ai_domestic_tick() {
    const int kings = scenario_defs[campaign.scenario].king_count;
    for(int king = 0; king < kings; ++king) {
        if(king == campaign.player) continue;
        // FUN_00402ba6 is called by the annual FUN_004032bb sweep. It visits
        // each employed officer assigned to a city and selects domestic work
        // from STR/INT; this must not be tied to army dispatch cadence.
        auto ai_search = [&](int city_index) {
            const int find_what=roll(100)>50 ? 1 : roll(100)/20;
            if(find_what==0) return;
            if(find_what==1) {
                int candidates[general_count],candidate_count=0;
                for(int candidate=0;candidate<general_count;++candidate)
                    if(campaign.officers[candidate].king==-1) candidates[candidate_count++]=candidate;
                if(!candidate_count) return;
                const int target=candidates[roll(candidate_count)];
                auto& recruit=campaign.officers[target];
                recruit.exp=recruited_experience(king);
                recruit.status=Status::employed;
                recruit.king=int8_t(king);
                recruit.city=int16_t(city_index);
                recruit.army=-1;
                recruit.loyalty=90;
                recruit.worked=1;
                native_origin_king(campaign,target)=uint8_t(king);
                native_status2_countdown(campaign,target)=0;
                check_level_up(target);
                return;
            }
            if(city_inventory_count(campaign,city_index)>=5) return;
            int type=0,index=0;
            if(find_what==2) { type=1; index=roll(31); }
            else if(find_what==3) { type=2; index=roll(11); }
            else { type=3; index=roll(7); }
            append_city_inventory_item(campaign,city_index,type,index);
        };
        for(int g = 0; g < general_count; ++g) {
            const auto& o = campaign.officers[g];
            if(o.king != king || o.status != Status::employed || o.army >= 0 ||
               o.city < 0 || o.city >= city_count) continue;
            const int str = general_defs[g].str, intel = general_defs[g].intel;
            int action=0;
            if(str>=85 || intel>=85) { ai_search(o.city); continue; }
            if(str>80) action=1;
            else if(intel>80) action=2;
            else continue;
            auto& city = campaign.cities[o.city];
            if(action == 1) {
                if(city.defense >= city_defense_cap) continue;
                const int amount=30+roll(10)+roll(maximum(1,str-80));
                city.defense=uint16_t(minimum(city_defense_cap,int(city.defense)+amount));
            } else {
                if(city.population >= city_population_cap) continue;
                const int amount=9000+roll(1000)+roll(maximum(1,(intel-80)*100));
                city.population=uint32_t(minimum<uint64_t>(city_population_cap,
                    uint64_t(city.population)+uint64_t(amount)));
            }
        }
        // demake IAOver applies at most one random city object to each stationed
        // general, keeping unsuitable equipment in the city's object list.
        auto equipment_profile=[](int item,int& type,int& amount) {
            if(item<12 || item>42) return false;
            if(item<=16) { type=0; amount=original::item_intelligence_bonus(item); }
            else if(item==17 || item==19) { type=1; amount=10; }
            else if(item==18) { type=2; amount=10; }
            else if(item<=38) { type=3; amount=original::item_strength_bonus(item); }
            else {
                type=4;
                constexpr int special[4]={3,4,2,1};
                amount=special[item-39];
            }
            return true;
        };
        auto ai_can_equip=[&](int general,int item) {
            int new_type=0,new_amount=0;
            if(!equipment_profile(item,new_type,new_amount)) return false;
            const int str=strength(general),intel=intelligence(general);
            if(new_type==3 && intel>70 && intel-str>=10) return false;
            if(new_type==0 && str>70 && str-intel>=10) return false;
            const int current=campaign.officers[general].equipped_item;
            if(!current) return true;
            int old_type=0,old_amount=0;
            if(!equipment_profile(current,old_type,old_amount)) return true;
            if(new_type==old_type) return new_amount>old_amount;
            if(intel>70 && intel-str>=10) return new_type==0;
            if(str>70 && str-intel>=10) return new_type==3;
            return true;
        };
        for(int city_index=0;city_index<city_count;++city_index) {
            if(campaign.cities[city_index].king!=king) continue;
            int roster[general_count];
            const int roster_count=officers_at(city_index,king,roster,general_count);
            for(int i=0;i<roster_count && city_inventory_count(campaign,city_index)>0;++i) {
                const int general=roster[i];
                const int object_index=roll(city_inventory_count(campaign,city_index));
                const uint8_t object=city_inventory_item(campaign,city_index,object_index);
                if(city_inventory_type(object)==1 &&
                   !ai_can_equip(general,12+city_inventory_index(object))) continue;
                const char* previous_notice=notice;
                use_city_object(general,city_index,object_index);
                notice=previous_notice;
            }
            // Preserve the domestic timing, but apply the native LOYAL gate
            // before the ordinary loyalty roll, for AI as well as players.
            for(int g=0;g<general_count;++g) {
                auto& prisoner=campaign.officers[g];
                if(prisoner.status!=Status::prisoner || prisoner.king!=king ||
                   prisoner.city!=city_index || prisoner.army>=0) continue;
                const int allegiance=prisoner_allegiance(campaign,g,king);
                if(allegiance<0) continue;
                if(allegiance>0 || roll(100)>prisoner.loyalty) {
                    prisoner.status=Status::employed;
                    prisoner.king=int8_t(king);
                    prisoner.city=int16_t(city_index);
                    prisoner.army=-1;
                    prisoner.loyalty=90;
                    native_origin_king(campaign,g)=uint8_t(king);
                    native_status2_countdown(campaign,g)=0;
                } else {
                    prisoner.loyalty=int8_t(clamp(int(prisoner.loyalty)-5-roll(15),0,100));
                }
            }
        }
    }
    // IAOverController applies an additional loyalty loss to every prisoner,
    // including those carried by an army or captured in another city.
    for(auto& officer:campaign.officers)
        if(officer.status==Status::prisoner)
            officer.loyalty=int8_t(clamp(int(officer.loyalty)-5-roll(15),0,100));
    for(int g=0;g<general_count;++g) {
        const auto& officer=campaign.officers[g];
        if(officer.king>=0 && officer.king!=campaign.player && officer.status!=Status::prisoner)
            ai_promote_general(g);
    }
}

void Game::finish_domestic(bool run_ai) {
    if(campaign.phase!=Phase::domestic) return;
    if(run_ai) ai_domestic_tick();
    campaign.phase=Phase::world;
    notice="大地图时间开始流动";
}

// A redirected route can span several cities. Check intermediate enemy
// gates without splitting friendly travel into extra 120-tick dispatch legs.
bool Game::intercept_army_city(int index) {
    auto& army=campaign.armies[index];
    if(army.retreating || !army.total || army_reverse_geometry(campaign,index) ||
       ((city_neighbors[army.origin]>>army.target)&1)) return false;
    const int length=city_path_length(army.origin,army.target);
    if(length<=0) return false;
    const uint32_t elapsed=army.total-army.remaining;
    const int before=int(uint32_t(length)*(elapsed-1)/army.total);
    const int after=int(uint32_t(length)*elapsed/army.total);
    if(before==after) return false;
    int current=army.origin,distance=0;
    for(int step=0;step<city_count;++step) {
        const int next=next_city_on_route(current,army.target);
        if(next<0 || next==army.target) break;
        distance+=city_route_lengths[current][next];
        if(distance>after) break;
        const int owner=campaign.cities[next].king;
        if(distance>before && owner>=0 && owner!=army.king) {
            army.origin=int8_t(current);army.target=int8_t(next);
            army.total=city_travel_ticks(current,next);army.remaining=0;
            army_pending_destination(campaign,index)=0;
            arrive(index);
            return true;
        }
        current=next;
    }
    return false;
}

void Game::world_tick() {
    if(campaign.phase != Phase::world) return;
    ++campaign.month_ticks;
    for(int king = 0; king < max_kings; ++king) if(campaign.ai_cooldown[king]) --campaign.ai_cooldown[king];
    recovery_tick();
    if(campaign.month_ticks % 30 == 0) ++campaign.day;
    if(campaign.month_ticks >= original::month_updates) {
        campaign.month_ticks = 0;
        campaign.day = 0;
        advance_month();
        if(campaign.phase != Phase::world) return;
    }
    // demake chooses AI city moves once per ordinary month in StrategyController.CityAct.
    MapPoint encounter_positions[max_armies];
    uint32_t positioned=0;
    auto position=[&](int index,const Army& army,uint32_t progress) -> const MapPoint& {
        const uint32_t bit=uint32_t(1)<<index;
#ifdef SANGO_REFERENCE_WORLD_ROUTING
        encounter_positions[index]=army_route_position(army,progress,army_reverse_geometry(campaign,index));
        (void)bit; (void)positioned;
#else
        if(!(positioned&bit)) { encounter_positions[index]=army_route_position(army,progress,army_reverse_geometry(campaign,index)); positioned|=bit; }
#endif
        return encounter_positions[index];
    };
    for(int i = 0; i < max_armies && campaign.phase == Phase::world; ++i) {
        auto& a = campaign.armies[i];
        if(a.retreating) army_pending_destination(campaign,i)&=0x80;
        if(a.active && !a.garrisoned) {
            if(a.remaining) {
                --a.remaining;
                if(intercept_army_city(i)) continue;
            }
            else {
                const int goal=army_destination(campaign,i);
                if(army_pending_destination(campaign,i) && goal!=a.target && !a.retreating) {
                    const int owner=campaign.cities[a.target].king;
                    if(owner>=0 && owner!=a.king) {
                        army_pending_destination(campaign,i)=0;
                        arrive(i);
                        continue;
                    }
                    a.origin=a.target; a.target=int8_t(goal);
                    a.total=a.remaining=city_travel_ticks(a.origin,goal);
                    army_pending_destination(campaign,i)=0;
                } else { army_pending_destination(campaign,i)=0; arrive(i); }
            }
        }
    }
    // Native 0x40d4e8 queues hostile army encounters after a map-unit collision
    // event; it does not require a reversed city link. Keep the known head-on
    // crossing rule and also catch co-location on the imported road geometry.
    // The native per-unit scheduler and collision radius remain unmodeled.
    for(int i = 0; i < max_armies && campaign.phase == Phase::world; ++i) {
        const auto& a = campaign.armies[i];
        if(!a.active || a.retreating == 1) continue;
        for(int j = i + 1; j < max_armies; ++j) {
            const auto& b = campaign.armies[j];
            if(!b.active || b.retreating == 1 || a.king == b.king || (a.garrisoned && b.garrisoned)) continue;
            const uint32_t total_a = uint32_t(maximum(1,int(a.total)));
            const uint32_t total_b = uint32_t(maximum(1,int(b.total)));
            const uint32_t progress_a = total_a - minimum(total_a,uint32_t(a.remaining));
            const uint32_t progress_b = total_b - minimum(total_b,uint32_t(b.remaining));
            bool encountered = false;
            if(!a.garrisoned && !b.garrisoned && a.origin == b.target && a.target == b.origin) {
                encountered = progress_a * total_b + progress_b * total_a >= total_a * total_b;
            }
            // Native 0x439661 queues on a collision event between hostile
            // moving units. For different city links, use one source-map-pixel
            // tolerance to absorb integer interpolation rounding, and avoid
            // treating shared city endpoints as a field encounter.
            if(!encountered && progress_a > 0 && progress_a < total_a &&
               progress_b > 0 && progress_b < total_b) {
                const MapPoint& pa = position(i,a,progress_a);
                const MapPoint& pb = position(j,b,progress_b);
                const int dx = pa.x-pb.x, dy = pa.y-pb.y;
                encountered = dx*dx+dy*dy <= 1;
            }
            if(encountered) {
                start_field_battle(i,j);
                return;
            }
        }
    }
    check_end();
}

void Game::arrive(int army_index) {
    auto& a = campaign.armies[army_index];
    army_pending_destination(campaign,army_index)=0;
    auto release_carried_prisoners = [&](int city) {
        for(auto& officer : campaign.officers) {
            if(officer.status != Status::prisoner || officer.army != army_index || officer.city != -1) continue;
            officer.king = a.king;
            officer.city = int16_t(city);
            officer.army = -1;
        }
    };
    if(a.retreating == 2) {
        const int current_city = a.target;
        if(current_city != a.retreat_goal && campaign.cities[current_city].king == a.king) {
            const int next = next_city_on_route(current_city,a.retreat_goal);
            if(next >= 0) {
                a.origin = int8_t(current_city);
                a.target = int8_t(next);
                a.total = city_travel_ticks(current_city,next);
                a.remaining = a.total;
                return;
            }
        }
        // The route ends at the selected native destination. At an owned
        // intermediate city it continues, while a hostile/neutral stop returns
        // to ordinary arrival handling so that the city can resolve it.
        if(current_city == a.retreat_goal || campaign.cities[current_city].king != a.king) {
            a.retreating = 0;
            a.retreat_goal = -1;
        }
    }
    if(a.retreating == 1) {
        const int current_city = a.target;
        if(current_city != a.retreat_goal && campaign.cities[current_city].king == a.king) {
            const int next = next_city_on_route(current_city,a.retreat_goal);
            if(next >= 0) {
                a.origin = int8_t(current_city);
                a.target = int8_t(next);
                a.total = city_travel_ticks(current_city,next);
                a.remaining = a.total;
                return;
            }
        }
        if(current_city == a.retreat_goal || campaign.cities[current_city].king != a.king ||
           a.retreat_goal < 0 || a.retreat_goal >= city_count) {
            // A hostile or neutral endpoint resumes ordinary arrival handling,
            // which can start the city battle/capture flow.
            a.retreating = 0;
            a.retreat_goal = -1;
        } else {
            // The selected goal is unreachable in the imported road graph.
            // Keep the surviving army at this friendly stop instead of deleting it.
            campaign.cities[current_city].money += a.money;
            release_carried_prisoners(current_city);
            for(int i = 0; i < a.count; ++i) {
                auto& officer = campaign.officers[a.generals[i]];
                officer.city = int16_t(current_city);
                officer.army = -1;
            }
            a.active = 0;
            return;
        }
    }
    if(campaign.cities[a.target].king == a.king) {
        // demake ArmyController.IntoTheCity admits only enough generals to
        // bring the employed garrison up to ten. demake admits the first
        // capacity entries (removing that prefix in reverse index order),
        // not the last entries. Preserve the remaining commander's order.
        int stationed[general_count];
        const int stationed_count = officers_at(a.target,a.king,stationed,general_count);
        int capacity=maximum(0,10-stationed_count),entered=0;
        const int faction_ruler = a.king >= 0 && a.king < scenario_defs[campaign.scenario].king_count ?
            king_defs[campaign.scenario][a.king].general : -1;
        while(entered<a.count && entered<capacity) {
            const int g=a.generals[entered]; auto& officer=campaign.officers[g];
            officer.city=a.target; officer.army=-1; set_native_escape_count(campaign,g,0);
            if(g==faction_ruler) campaign.cities[a.target].major=int16_t(g);
            ++entered;
        }
        for(int i=entered;i<a.count;++i) a.generals[i-entered]=a.generals[i];
        a.count=uint8_t(int(a.count)-entered);
        if(a.count==0) {
            campaign.cities[a.target].money+=a.money;
            release_carried_prisoners(a.target);
            a.active=0;
        } else {
            // SetArmyGarrison leaves the army and its money/prisoners at the
            // city while the generals that fit join the city roster.
            a.origin=a.target;
            a.total=0; a.remaining=0; a.retreating=0; a.retreat_goal=-1;
            a.garrisoned=1;
        }
        return;
    }
    int defenders[general_count];
    int count = officers_at(a.target, campaign.cities[a.target].king, defenders, general_count);
    if(count == 0 && (a.king == campaign.player || campaign.cities[a.target].king < 0 ||
                      campaign.cities[a.target].king == campaign.player)) {
        const bool unowned_city=campaign.cities[a.target].king<0;
        const bool player_army=a.king==campaign.player;
        // Keep player-involved and unowned empty-city captures immediate;
        // start_battle would otherwise try a duel without a defender. Native
        // AI battles against an owned enemy city still run auto settlement.
        battle = {};
        battle.army = int8_t(army_index);
        battle.city = a.target;
        finish_battle(0);
        // demake takes an unowned city directly, without a war dialogue.
        if(unowned_city && player_army) notice="已占领无主城池";
    } else start_battle(army_index);
}

void Game::start_battle(int army_index) {
    battle = {};
    const auto& a = campaign.armies[army_index];
    battle.army = int8_t(army_index);
    battle.opponent_army = -1;
    battle.city = a.target;
    battle.terrain = original::terrain_at(city_defs[a.target].x,city_defs[a.target].y);
    battle.winner = -1;
    battle.pending_winner = -1;
    battle.attacker_count = int8_t(a.count);
    for(int i = 0; i < a.count; ++i) {
        battle.attackers[i] = a.generals[i];
        const auto& officer = campaign.officers[a.generals[i]];
        battle.starting_front[0][i] = officer.front;
        battle.starting_rear[0][i] = uint16_t(officer.soldiers-officer.front);
    }
    int defenders[general_count];
    int count = officers_at(a.target, campaign.cities[a.target].king, defenders, general_count);
    battle.defender_count = int8_t(minimum(count, max_battle_generals));
    for(int i = 0; i < battle.defender_count; ++i) if(defenders[i] == campaign.cities[a.target].major) {
        int first = defenders[0];
        defenders[0] = defenders[i];
        defenders[i] = first;
        break;
    }
    for(int i = 0; i < battle.defender_count; ++i) {
        battle.defenders[i] = int16_t(defenders[i]);
        const auto& officer = campaign.officers[defenders[i]];
        battle.starting_front[1][i] = officer.front;
        battle.starting_rear[1][i] = uint16_t(officer.soldiers-officer.front);
    }
    battle.roster_active_count[0] = uint8_t(battle.attacker_count);
    battle.roster_active_count[1] = uint8_t(battle.defender_count);
    for(int i = 0; i < battle.attacker_count; ++i) battle.roster_active[0][i] = 1;
    for(int i = 0; i < battle.defender_count; ++i) battle.roster_active[1][i] = 1;
    if(a.king != campaign.player && campaign.cities[a.target].king != campaign.player) {
        resolve_demake_ai_city_battle(army_index);
        return;
    }
    const int player_side = a.king == campaign.player ? 0 : 1;
    const int ai_side = 1-player_side;
    // The original battle roster starts at its first entry. City battles put
    // the city major first above, so the opponent's opening general is already
    // determined by the native roster ordering; automatic weighted rotation
    // only happens after a duel has been resolved.
    if(ai_side == 0) battle.attacker_index = 0;
    else battle.defender_index = 0;
    // Let the player choose the first general before every battle. Large
    // defending rosters can also assign each participant to a row.
    battle.row_setup_active = uint8_t(a.king == campaign.player ||
                                      campaign.cities[a.target].king == campaign.player);
    campaign.phase = Phase::battle;
    start_duel(false);
    battle.row_setup_active = uint8_t(a.king == campaign.player ||
                                      campaign.cities[a.target].king == campaign.player);
    notice = "战斗开始  A武将技能  Start部队指令";
}

void Game::start_field_battle(int first_army, int second_army) {
    battle = {};
    const auto& first = campaign.armies[first_army];
    const auto& second = campaign.armies[second_army];
    battle.army = int8_t(first_army);
    battle.opponent_army = int8_t(second_army);
    battle.city = -1;
    battle.field_battle = 1;
    const MapPoint first_position = army_position(campaign,first_army);
    const MapPoint second_position = army_position(campaign,second_army);
    battle.terrain = original::terrain_at((first_position.x+second_position.x)/2,
                                          (first_position.y+second_position.y)/2);
    battle.winner = -1;
    battle.pending_winner = -1;
    battle.attacker_count = int8_t(minimum(int(first.count),5));
    battle.defender_count = int8_t(minimum(int(second.count),5));
    for(int i = 0; i < battle.attacker_count; ++i) {
        battle.attackers[i] = first.generals[i];
        const auto& officer = campaign.officers[first.generals[i]];
        battle.starting_front[0][i] = officer.front;
        battle.starting_rear[0][i] = uint16_t(officer.soldiers-officer.front);
    }
    for(int i = 0; i < battle.defender_count; ++i) {
        battle.defenders[i] = second.generals[i];
        const auto& officer = campaign.officers[second.generals[i]];
        battle.starting_front[1][i] = officer.front;
        battle.starting_rear[1][i] = uint16_t(officer.soldiers-officer.front);
    }
    battle.roster_active_count[0] = uint8_t(battle.attacker_count);
    battle.roster_active_count[1] = uint8_t(battle.defender_count);
    for(int i = 0; i < battle.attacker_count; ++i) battle.roster_active[0][i] = 1;
    for(int i = 0; i < battle.defender_count; ++i) battle.roster_active[1][i] = 1;
    if(first.king != campaign.player && second.king != campaign.player) {
        finish_battle(resolve_demake_ai_field_battle(first_army,second_army),true);
        return;
    }
    const int player_side = first.king == campaign.player ? 0 : 1;
    const int ai_side = 1-player_side;
    if(ai_side == 0) battle.attacker_index = 0;
    else battle.defender_index = 0;
    campaign.phase = Phase::battle;
    start_duel(false);
    battle.row_setup_active = 1;
    notice = "行军部队遭遇，野战开始";
}

void Game::resolve_demake_ai_city_battle(int army_index) {
    auto& army=campaign.armies[army_index];
    const int target=battle.city, attacker_king=army.king, defender_king=campaign.cities[target].king;
    auto& city=campaign.cities[target];
    int attacker_index=0,defender_index=0;
    while(attacker_index<battle.attacker_count && defender_index<battle.defender_count) {
        const int attacker=battle.attackers[attacker_index], defender=battle.defenders[defender_index];
        const int attacker_power=level(attacker)*5+int(campaign.officers[attacker].soldiers)*3+
            strength(attacker)*2+campaign.officers[attacker].hp+campaign.officers[attacker].mp/2+roll(20);
        const int defender_power=level(defender)*5+int(campaign.officers[defender].soldiers)*3+
            strength(defender)*2+campaign.officers[defender].hp+campaign.officers[defender].mp/2+
            city.defense/10+roll(20);
        const bool attack_won=attacker_power>defender_power;
        const int winner_id=attack_won ? attacker : defender;
        const int loser_id=attack_won ? defender : attacker;
        auto& winner=campaign.officers[winner_id];
        auto& loser=campaign.officers[loser_id];
        loser.front=0; loser.soldiers=0; loser.hp=2; loser.mp=2;
        demake_battle_capture(*this,loser_id,winner.king,false);
        const uint32_t award=(demake_level_experience(level(winner_id)+1)-
                              demake_level_experience(level(winner_id)))/2;
        if(award) gain_experience(winner_id,int(award));
        int damage=(attack_won ? defender_power : attacker_power)-strength(winner_id);
        if(damage>winner.soldiers*3) { damage-=winner.soldiers*3; winner.soldiers=0; winner.front=0; }
        else { winner.soldiers=uint16_t(maximum(0,int(winner.soldiers)-damage/3)); winner.front=uint16_t(minimum(int(winner.front),int(winner.soldiers))); damage=0; }
        if(damage>winner.mp/2) { damage-=winner.mp/2; winner.mp=0; }
        else { winner.mp=uint16_t(maximum(0,int(winner.mp)-damage*2)); damage=0; }
        if(damage) winner.hp=uint16_t(maximum(15,int(winner.hp)-damage));
        if(attack_won) ++defender_index; else ++attacker_index;
    }
    const bool attacker_won=defender_index>=battle.defender_count;
    battle.winner=attacker_won ? 0 : 1;
    if(attacker_won) {
        for(int i=0;i<army.count;++i) {
            const int g=army.generals[i];
            auto& officer=campaign.officers[g];
            officer.status=Status::employed; officer.king=int8_t(attacker_king);
            officer.city=int16_t(target); officer.army=-1;
            native_origin_king(campaign,g)=uint8_t(attacker_king);
            set_native_escape_count(campaign,g,0);
            const uint32_t award=level(g)>4 ?
                (demake_level_experience(level(g)+1)-demake_level_experience(level(g)))/4 : 50;
            if(award) gain_experience(g,int(award));
        }
        int evacuees[general_count],evacuee_count=0;
        for(int g=0;g<general_count;++g) {
            auto& officer=campaign.officers[g];
            if(officer.city!=target) continue;
            if(officer.status==Status::prisoner) {
                officer.king=int8_t(attacker_king); officer.army=-1;
            } else if(officer.status==Status::employed && officer.king==defender_king) evacuees[evacuee_count++]=g;
        }
        int refugee_count=0;
        while(refugee_count<evacuee_count) {
            int free_army=-1;
            for(int i=0;i<max_armies;++i) if(!campaign.armies[i].active && i!=army_index) { free_army=i; break; }
            if(free_army<0) break;
            int candidates[max_city_neighbors],candidate_count=0;
            for(int i=0;i<city_neighbor_counts[target];++i) {
                const int neighbor=city_neighbor_order[target][i];
                if(campaign.cities[neighbor].king==defender_king) candidates[candidate_count++]=neighbor;
            }
            if(!candidate_count) for(int i=0;i<city_neighbor_counts[target];++i) {
                const int neighbor=city_neighbor_order[target][i];
                if(campaign.cities[neighbor].king<0) candidates[candidate_count++]=neighbor;
            }
            if(!candidate_count) for(int i=0;i<city_neighbor_counts[target];++i) candidates[candidate_count++]=city_neighbor_order[target][i];
            const int destination=candidates[roll(candidate_count)];
            auto& refugee=campaign.armies[free_army]; refugee={};
            army_pending_destination(campaign,free_army)=0;
            refugee.king=int8_t(defender_king); refugee.origin=int8_t(target); refugee.target=int8_t(destination);
            refugee.count=uint8_t(minimum(5,evacuee_count-refugee_count));
            refugee.total=city_travel_ticks(target,destination); refugee.remaining=refugee.total;
            refugee.retreat_goal=int8_t(destination); refugee.retreating=2; refugee.active=1;
            for(int j=0;j<refugee.count;++j) {
                const int g=evacuees[evacuee_count-1-refugee_count++];
                refugee.generals[j]=int16_t(g); campaign.officers[g].city=-1; campaign.officers[g].army=int16_t(free_army);
            }
        }
        while(refugee_count<evacuee_count) {
            const int g=evacuees[evacuee_count-1-refugee_count++];
            auto& officer=campaign.officers[g]; officer.status=Status::prisoner;
            officer.king=int8_t(attacker_king); officer.city=int16_t(target); officer.army=-1; officer.loyalty=30;
        }
        city.king=int8_t(attacker_king); city.money+=army.money;
        if(army.count) city.major=army.generals[0];
        for(auto& officer:campaign.officers)
            if(officer.status==Status::prisoner && officer.army==army_index && officer.city==-1) {
                officer.army=-1; officer.city=int16_t(target); officer.king=int8_t(attacker_king);
            }
        army.money=0; army.active=0;
    } else {
        // demake restores every member of the city's original general list on
        // a defense win, including defenders captured earlier in this same
        // attack. `officers_at` omits those now marked as prisoners.
        for(int i=0;i<battle.defender_count;++i) {
            const int g=battle.defenders[i]; auto& officer=campaign.officers[g];
            officer.status=Status::employed; officer.king=int8_t(defender_king);
            officer.city=int16_t(target); officer.army=-1;
            const uint32_t award=level(g)>4 ?
                (demake_level_experience(level(g)+1)-demake_level_experience(level(g)))/4 : 50;
            if(award) gain_experience(g,int(award));
        }
        int survivors[5],survivor_count=0;
        for(int i=0;i<army.count;++i) {
            const int g=army.generals[i]; auto& officer=campaign.officers[g];
            if(officer.status==Status::prisoner && officer.king==defender_king) {
                officer.city=int16_t(target); officer.army=-1;
            } else survivors[survivor_count++]=g;
        }
        if(survivor_count) {
            const int home=army.origin; army.count=uint8_t(survivor_count);
            for(int i=0;i<survivor_count;++i) {
                army.generals[i]=int16_t(survivors[i]);
                campaign.officers[survivors[i]].city=-1; campaign.officers[survivors[i]].army=int16_t(army_index);
            }
            army.origin=int8_t(target); army.target=int8_t(home); army.total=city_travel_ticks(target,home);
            army.remaining=army.total; army.retreat_goal=int8_t(home); army.retreating=2;
        } else {
            for(auto& officer:campaign.officers)
                if(officer.status==Status::prisoner && officer.army==army_index && officer.city==-1) {
                    officer.army=-1; officer.city=int16_t(target); officer.king=int8_t(defender_king);
                }
            city.money+=army.money; army.money=0; army.active=0;
        }
    }
    campaign.phase=Phase::world;
    check_end();
}

int Game::resolve_demake_ai_field_battle(int first_army,int second_army) {
    auto& first_army_info=campaign.armies[first_army];
    auto& second_army_info=campaign.armies[second_army];
    int first_index=0,second_index=0;
    while(first_index<first_army_info.count && second_index<second_army_info.count) {
        const int first=battle.attackers[first_index],second=battle.defenders[second_index];
        const int first_power=level(first)*5+int(campaign.officers[first].soldiers)*3+
            strength(first)*2+campaign.officers[first].hp+campaign.officers[first].mp/2+roll(20);
        const int second_power=level(second)*5+int(campaign.officers[second].soldiers)*3+
            strength(second)*2+campaign.officers[second].hp+campaign.officers[second].mp/2+roll(20);
        const bool first_won=first_power>second_power;
        const int winner_id=first_won ? first : second;
        const int loser_id=first_won ? second : first;
        auto& winner=campaign.officers[winner_id];
        auto& loser=campaign.officers[loser_id];
        loser.front=0; loser.soldiers=0; loser.hp=2; loser.mp=2;
        demake_battle_capture(*this,loser_id,winner.king,false);
        const uint32_t award=(demake_level_experience(level(winner_id)+1)-
                              demake_level_experience(level(winner_id)))/2;
        if(award) gain_experience(winner_id,int(award));
        int damage=(first_won ? second_power : first_power)-strength(winner_id);
        if(damage>winner.soldiers*3) {
            damage-=winner.soldiers*3; winner.soldiers=0; winner.front=0;
        } else {
            winner.soldiers=uint16_t(maximum(0,int(winner.soldiers)-damage/3));
            winner.front=uint16_t(minimum(int(winner.front),int(winner.soldiers))); damage=0;
        }
        if(damage>winner.mp/2) { damage-=winner.mp/2; winner.mp=0; }
        else { winner.mp=uint16_t(maximum(0,int(winner.mp)-damage*2)); damage=0; }
        if(damage) winner.hp=uint16_t(maximum(15,int(winner.hp)-damage));
        if(first_won) ++second_index; else ++first_index;
    }
    return second_index>=second_army_info.count ? 0 : 1;
}

int Game::resolve_ai_battle() {
    // FUN_0040f4f0 routes battles without the player through
    // FUN_0040e5df/FUN_0040e6d1 and the roster-pair resolver FUN_0040f44a.
    uint8_t active[2][max_battle_generals]{};
    int cursor[2]{};
    for(int side = 0; side < 2; ++side) {
        const int count = side == 0 ? battle.attacker_count : battle.defender_count;
        for(int i = 0; i < count; ++i) active[side][i] = 1;
    }
    auto roster_general = [&](int side,int index) {
        return side == 0 ? int(battle.attackers[index]) : int(battle.defenders[index]);
    };
    auto next_participant = [&](int side) {
        const int count = side == 0 ? int(battle.attacker_count) : int(battle.defender_count);
        bool any = false;
        for(int i = 0; i < count; ++i) any |= active[side][i] != 0;
        if(!any || count <= 0) return -1;
        const int advances = 5 + roll(10); // FUN_0040ec7a: 5..14 advances.
        for(int step = 0; step < advances; ++step) {
            for(int probe = 0; probe < count; ++probe) {
                cursor[side] = (cursor[side] + 1) % count;
                if(active[side][cursor[side]]) break;
            }
        }
        return cursor[side];
    };
    auto effective_troops = [](const Officer& officer) {
        return int(officer.front) + 2*maximum(0,int(officer.soldiers)-int(officer.front));
    };
    auto apply_losses = [](Officer& officer,int front_loss,int rear_loss) {
        const int front = maximum(0,int(officer.front)-maximum(0,front_loss));
        const int rear = maximum(0,int(officer.soldiers)-int(officer.front)-maximum(0,rear_loss));
        officer.front = uint16_t(front);
        officer.soldiers = uint16_t(front+rear);
    };
    auto award = [&](int winner_side,int winner_index,int loser_side,int loser_index,int mode) {
        const int winner_id = roster_general(winner_side,winner_index);
        const int loser_id = roster_general(loser_side,loser_index);
        const int winner_losses = battle_casualty_experience(winner_side,winner_index);
        const int loser_losses = battle_casualty_experience(loser_side,loser_index);
        if(mode == 1) {
            add_battle_experience(winner_side,winner_index,loser_losses+51*level(loser_id));
            add_battle_experience(loser_side,loser_index,winner_losses/2);
        } else {
            add_battle_experience(winner_side,winner_index,loser_losses+34*level(loser_id));
            add_battle_experience(loser_side,loser_index,winner_losses+(17*level(winner_id))/2);
        }
        bool capture = mode == 1;
        const auto& loser = campaign.officers[loser_id];
        const bool is_faction_leader = loser.king >= 0 && loser.king < scenario_defs[campaign.scenario].king_count &&
            king_defs[campaign.scenario][loser.king].general == loser_id;
        if(capture && is_faction_leader && loser.king != campaign.player) {
            // In the native automatic settlement, FUN_0040d932 preserves an
            // AI ruler when that faction still has an army in its linked list.
            // Both armies are still active until finish_battle settles them.
            for(const auto& army : campaign.armies)
                if(army.active && army.king == loser.king) { capture = false; break; }
        }
        if(capture) {
            campaign.officers[loser_id].status = Status::prisoner;
            native_status2_countdown(campaign,loser_id) = general_defs[loser_id].constancy;
            if(battle.captured_count < max_battle_captured)
                battle.captured[battle.captured_count++] = int16_t(loser_id);
        }
        active[loser_side][loser_index] = 0;
    };
    auto magic_attack = [&](int attacker_id,int defender_id) {
        auto& attacker = campaign.officers[attacker_id];
        auto& defender = campaign.officers[defender_id];
        if(attacker.mp <= 29) return false;
        attacker.mp = uint16_t(attacker.mp-30);
        const int damage = intelligence(attacker_id)*20/100;
        const int rear = maximum(0,int(defender.soldiers)-int(defender.front));
        const int total = int(defender.front)+rear;
        if(damage < total) {
            if(total > 0) {
                const int rear_loss = rear*damage/total;
                apply_losses(defender,damage-rear_loss,rear_loss);
            }
            return false;
        }
        defender.front = 0;
        defender.soldiers = 0;
        const int hp_loss = (damage-total)*max_hp(defender_id)/100;
        defender.hp = uint16_t(maximum(0,int(defender.hp)-hp_loss));
        return defender.hp < 1;
    };
    auto pair_fight = [&](int first_index,int second_index) {
        const int first_id = roster_general(0,first_index);
        const int second_id = roster_general(1,second_index);
        int magic_winner = -1;
        if(roll(2) == 0) {
            if(magic_attack(second_id,first_id)) magic_winner = 1;
            else if(magic_attack(first_id,second_id)) magic_winner = 0;
        } else {
            if(magic_attack(first_id,second_id)) magic_winner = 0;
            else if(magic_attack(second_id,first_id)) magic_winner = 1;
        }
        if(magic_winner >= 0) {
            if(magic_winner == 0) award(0,first_index,1,second_index,1);
            else award(1,second_index,0,first_index,1);
            return;
        }

        // FUN_0040eebb equalizes front + 2*rear troop strength.
        auto& first = campaign.officers[first_id];
        auto& second = campaign.officers[second_id];
        int first_power = effective_troops(first), second_power = effective_troops(second);
        if(first_power || second_power) {
            if(first_power < second_power) {
                first.front = 0; first.soldiers = 0;
                const int rear_loss = first_power*maximum(0,int(second.soldiers)-int(second.front))/second_power;
                apply_losses(second,first_power-2*rear_loss,rear_loss);
            } else {
                second.front = 0; second.soldiers = 0;
                const int rear_loss = second_power*maximum(0,int(first.soldiers)-int(first.front))/first_power;
                apply_losses(first,second_power-2*rear_loss,rear_loss);
            }
        }

        int attack_side = 0, attack_index = first_index, defend_side = 1, defend_index = second_index;
        first_power = effective_troops(first);
        second_power = effective_troops(second);
        if(first_power < 1 && second_power > 0) {
            attack_side = 1; attack_index = second_index; defend_side = 0; defend_index = first_index;
        }
        const int attacker_id = roster_general(attack_side,attack_index);
        const int defender_id = roster_general(defend_side,defend_index);
        auto& attacker = campaign.officers[attacker_id];
        auto& defender = campaign.officers[defender_id];
        const int attack_power = effective_troops(attacker);
        const int damage = (attack_power*20*(75-strength(defender_id)/2))/100;
        if(attack_power > 0 && damage >= int(defender.hp)) {
            const int remaining_power = int(defender.hp)*attack_power/damage;
            const int rear = maximum(0,int(attacker.soldiers)-int(attacker.front));
            const int rear_loss = rear*remaining_power/attack_power;
            apply_losses(attacker,remaining_power-2*rear_loss,rear_loss);
            defender.hp = 0;
            const int threshold = clamp(effective_troops(attacker),10,40);
            const int mode = roll(50) < threshold ? 1 : 2;
            award(attack_side,attack_index,defend_side,defend_index,mode);
            return;
        }
        if(attack_power > 0) {
            attacker.front = 0;
            attacker.soldiers = 0;
            defender.hp = uint16_t(maximum(1,int(defender.hp)-damage));
        }

        // FUN_0040f2dc settles a remaining pair by HP and gives mode 1 on 1/8.
        const int hp_winner_side = first.hp >= second.hp ? 0 : 1;
        const int hp_winner_index = hp_winner_side == 0 ? first_index : second_index;
        const int hp_loser_side = 1-hp_winner_side;
        const int hp_loser_index = hp_loser_side == 0 ? first_index : second_index;
        auto& hp_winner = campaign.officers[roster_general(hp_winner_side,hp_winner_index)];
        auto& hp_loser = campaign.officers[roster_general(hp_loser_side,hp_loser_index)];
        const int hp_gap = int(hp_winner.hp)-int(hp_loser.hp);
        hp_winner.hp = uint16_t(minimum(int(hp_winner.hp),maximum(16,hp_gap)));
        const int result_roll = roll(8);
        hp_loser.hp = uint16_t(result_roll*2);
        award(hp_winner_side,hp_winner_index,hp_loser_side,hp_loser_index,result_roll == 0 ? 1 : 2);
    };

    int first_index = next_participant(0);
    int second_index = next_participant(1);
    while(first_index >= 0 && second_index >= 0) {
        pair_fight(first_index,second_index);
        first_index = next_participant(0);
        second_index = next_participant(1);
    }
    const int winner = first_index < 0 && second_index < 0 ? 2 : first_index < 0 ? 1 : 0;
    if(!battle.field_battle && battle.city >= 0 && battle.city < city_count) {
        auto& city = campaign.cities[battle.city];
        if(winner == 0) {
            // FUN_0040e13f sets remaining money to 30-50% of the old value
            // (so the actual loss is 50-70%), then removes 15-20% of
            // population and defense and clears the city reserve.
            city.money = scale_32(city.money,uint32_t(3000+roll(2000)),10000u);
            const uint32_t population_loss = scale_32(city.population,uint32_t(1500+roll(500)),10000u);
            const uint32_t defense_loss = (uint32_t(1500+roll(500))*city.defense)/10000u;
            city.population -= population_loss;
            city.defense = uint16_t(uint32_t(city.defense)-defense_loss);
            city.reserve = 0;
        } else if(winner == 1) {
            // FUN_0040e0c4: a successfully defended city loses 5-7% of
            // population and defense.
            const uint32_t population_loss = scale_32(city.population,uint32_t(500+roll(200)),10000u);
            const uint32_t defense_loss = (uint32_t(500+roll(200))*city.defense)/10000u;
            city.population -= population_loss;
            city.defense = uint16_t(uint32_t(city.defense)-defense_loss);
        }
    }
    return winner;
}

void Game::start_duel(bool confirmed) {
    for(auto& bits:battle.magic_delay_bits) bits=0;
    battle.round_result_level_before=0;
    battle.row_setup_active = 0;
    battle.camera_manual = 0;
    battle.camera_y_manual = 0;
    battle.camera_y = battle_commander_y;
    battle.duel_tick = 0;
    battle.pending_winner = -1;
    battle.effect_ticks = 0;
    battle.effect_remaining = 0;
    battle.effect_side = battle.effect_kind = battle.effect_applied = battle.paused = 0;
    battle.effect_script = 0;
    battle.heal_wait = 0;
    battle.heal_remainder_wait = 0;
    battle.heal_visual_remaining = 0;
    battle.heal_steps = battle.heal_remainder = 0;
    for(auto& immunity : battle.immunity_ticks) immunity = 0;
    battle.magic_callback_damage = 0;
    battle.magic_field_iterations = battle.magic_field_wait = battle.magic_field_state = 0;
    battle.magic_composite_kind = battle.magic_composite_stage = 0;
    battle.magic_composite_side = battle.magic_composite_bootstrap = 0;
    battle.magic_composite_wait = 0;
    battle.magic_field_center_x = battle.magic_field_center_y = 0;
    battle.magic_field_origin_x = battle.magic_field_origin_y = 0;
    for(int side = 0; side < 2; ++side) {
        const int g = side ? battle.defenders[battle.defender_index] :
                             battle.attackers[battle.attacker_index];
        battle.ai_decision_countdown[side] = campaign.officers[g].king == campaign.player ?
            uint16_t(original::battle_ai_player_first_decision_updates) :
            uint16_t(original::battle_ai_opponent_first_decision_updates);
    }
    for(auto& projectile : battle.projectiles) projectile = {};
    for(auto& emitter : battle.m014_spark_emitters) emitter = {};
    for(int side = 0; side < 2; ++side) {
        battle.general_hit_ticks[side] = 0;
        battle.general_hit_damage[side] = 0;
        auto& f = battle.sides[side];
        auto& commander_visual = battle.general_visual[side];
        commander_visual = {};
        commander_visual.facing = uint8_t(side != 0);
        const int roster_index = side ? int(battle.defender_index) : int(battle.attacker_index);
        int g = side ? battle.defenders[roster_index] : battle.attackers[roster_index];
        auto& o = campaign.officers[g];
        // demake WarSceneController.Init: once per confirmed bout, capped.
        if(confirmed) {
            o.hp=uint16_t(minimum(max_hp(g),int(o.hp)+10));
            o.mp=uint16_t(minimum(max_mp(g),int(o.mp)+5));
        }
        f = {};
        f.general = int16_t(g);
        f.hp = int16_t(o.hp);
        battle.starting_hp[side]=uint16_t(f.hp);
        f.initial_hp = int16_t(max_hp(g));
        f.mp = int16_t(o.mp);
        f.soldiers = int16_t(o.soldiers);
        f.initial_soldiers = int16_t(max_soldiers(g));
        f.front = int16_t(o.front);
        f.force = o.force;
        f.side_bonus = !battle.field_battle && side == 1 && battle.city >= 0 ?
            int16_t(campaign.cities[battle.city].defense) : 0;
        // Each player-controlled matchup waits for an explicit command.
        // The opposing army keeps its automatic offensive order.
        f.order = Order::hold;
        if(o.king!=campaign.player && confirmed) f.ai_action_flags=uint32_t((8+roll(7))*30)<<8;
        else f.ai_action_flags=fighter_opening_finished_flag;
        // Native battle deployment places the attackers on the right facing
        // left and the defenders on the left facing right.
        f.facing = uint8_t(side != 0);
        f.attack_direction = int8_t(side ? 1 : -1);
        f.charge = original::cast_charge_initial;
        const int native_general_x = side ? 11 : 45;
        const int front_offset = battle.general_front[side][roster_index] ?
            battle_general_front_offset : 0;
        f.x = int16_t(battlefield_width/2+(native_general_x-28)*battle_grid_column_width+
                      (side ? front_offset : -front_offset));
        f.previous_x = f.x;
        f.deployment_x = f.x;
        const int original_front = int(f.front);
        const int rear_count = int(f.soldiers)-original_front;
        for(int i = 0; i < 200; ++i) {
            auto& s = battle.soldiers[side * 200 + i];
            const bool rear = i >= original_front;
            if(i < f.soldiers) {
                const int rank_index = rear ? i-original_front : i;
                const int layout = int(o.formation) +
                    (battle.layout_front[side][roster_index] ? 7 : 0);
                const FormationPoint point = formation_soldier_point(
                    layout,rear,rank_index,original_front,rear_count,
                    &s.formation_column);
                // Keep the native 56x14 formation cells distinct. The 30:40
                // pitch approximates the native 72:96 cell proportions; the
                // vertical camera exposes a few rows at a time.
                const int native_x = side ? int(point.x) : 56-int(point.x);
                s.x = int16_t(clamp(battlefield_width/2+(native_x-28)*battle_grid_column_width,0,battlefield_width-1));
                s.y = int16_t(int(point.y)*battle_grid_row_height);
            } else {
                s.x = f.x;
                s.y = battle_commander_y;
            }
            s.previous_x = s.x;
            s.previous_y = s.y;
            // One live marker per soldier. ForceSoldierObjectProcess handles
            // 0x10004 by immediately sending 0x10003 to remove the unit.
            s.hp = int8_t(i < f.soldiers ? 1 : 0);
            s.side = uint8_t(side);
            s.index = uint8_t(i);
            s.rear = uint8_t(i >= f.front);
            s.move_target_x = s.x;
            s.move_target_y = s.y;
            s.move_dx = s.move_dy = 0;
            s.move_x_remainder = s.move_y_remainder = 0;
            s.move_ticks = 0;
            s.action_ticks = s.action_delay = 0;
            s.move_command8 = 0;
            s.attack_ticks = 0;
            s.attack_direction = int8_t(side ? 1 : -1);
            auto& visual = battle.soldier_visual[side * 200 + i];
            visual = {};
            visual.facing = uint8_t(side != 0);
            visual.walk_tick = uint8_t(i % 3);
            visual.walk_frame = uint8_t((i / 3) & 3);
        }
    }
    battle.sides[0].soldier_hit_threshold = int16_t(original::soldier_hit_threshold(
        battle.sides[0].force,battle.sides[1].force,battle.terrain,battle.sides[0].side_bonus));
    battle.sides[1].soldier_hit_threshold = int16_t(original::soldier_hit_threshold(
        battle.sides[1].force,battle.sides[0].force,battle.terrain,battle.sides[1].side_bonus));
}

int Game::select_active_battle_general(int side) {
    const int count = side == 0 ? int(battle.attacker_count) : int(battle.defender_count);
    int weights[max_battle_generals]{};
    uint32_t total = 0;
    for(int i = 0; i < count; ++i) {
        if(!battle.roster_active[side][i]) continue;
        const int g = side == 0 ? int(battle.attackers[i]) : int(battle.defenders[i]);
        const auto& o = campaign.officers[g];
        // FUN_00404851 sums current HP, MP, STR, INT, front troops, and rear
        // troops. In the default native mode the first roster weight is capped
        // at 10 when DAT_004984d8 bit 0x100 is clear.
        int weight = int(o.hp) + int(o.mp) + strength(g) + intelligence(g) + int(o.soldiers);
        if(i == 0) weight = minimum(weight,10);
        weights[i] = maximum(weight,0);
        total += uint32_t(weights[i]);
    }
    if(total > 0) {
        int draw = roll(int(total));
        for(int i = 0; i < count; ++i) {
            if(!battle.roster_active[side][i]) continue;
            if(draw < weights[i]) return i;
            draw -= weights[i];
        }
    }

    // FUN_0040ec7a's zero-weight fallback advances 5..14 times to the next
    // available roster entry, wrapping at the end.
    if(battle.roster_active_count[side] == 0) return -1;
    int selected = -1;
    const int advances = 5 + roll(10);
    for(int step = 0; step < advances; ++step) {
        for(int offset = 1; offset <= count; ++offset) {
            const int candidate = (selected + offset + count) % count;
            if(battle.roster_active[side][candidate]) {
                selected = candidate;
                break;
            }
        }
    }
    return selected;
}

void Game::casualties(int side, int number) {
    for(int i = 199; i >= 0 && number > 0; --i) {
        auto& s = battle.soldiers[side * 200 + i];
        if(s.hp > 0) { s.hp = 0; --battle.sides[side].soldiers; if(!s.rear) --battle.sides[side].front; --number; }
    }
}

bool Game::issue_battle_order(int side,Order order) {
    if(side<0 || side>1 || int(order)>int(Order::front_back) ||
       campaign.phase!=Phase::battle || battle.pending_winner>=0 || battle.sides[side].hp<=0) return false;
    auto& fighter=battle.sides[side];
    const bool changed=fighter.order!=order;
    if(!changed && order!=Order::hold) return true;
    fighter.order=order;
    if(order==Order::hold) for(auto& command:fighter.ai_command_slots) command=0;
    if(order!=Order::advance && order!=Order::general) fighter.move_command8=0;
    if(changed && !fighter.attack_ticks && !battle.magic_delay_active(side)) fighter.action_ticks=0;
    for(int i=side*200;i<(side+1)*200;++i) {
        auto& soldier=battle.soldiers[i];
        if(soldier.hp>0 && order==Order::hold) {
            // SetIdle stops the current move immediately, including a
            // formation adjustment while the army was already on hold.
            soldier.move_ticks=0;soldier.move_dx=soldier.move_dy=0;
            soldier.move_x_remainder=soldier.move_y_remainder=0;
            soldier.move_target_x=soldier.x;soldier.move_target_y=soldier.y;
            soldier.move_command8=0;
        }
        const bool front_rank=soldier_in_front_rank(soldier);
        const bool moving=order==Order::advance || (order==Order::front && front_rank) ||
            (order==Order::rear && !front_rank) || order==Order::retreat ||
            (order==Order::front_back && front_rank);
        if(soldier.hp>0 && moving && !soldier.attack_ticks && !battle.magic_delay_active(2+i)) soldier.action_ticks=0;
    }
    return true;
}

bool Game::issue_battle_command(int side,BattleCommand command) {
    if(side<0 || side>1 || int(command)>int(BattleCommand::retreat) ||
       campaign.phase!=Phase::battle || battle.pending_winner>=0 || battle.sides[side].hp<=0) return false;
    switch(command) {
    case BattleCommand::hold: return issue_battle_order(side,Order::hold);
    case BattleCommand::assault: return issue_battle_order(side,Order::advance);
    case BattleCommand::front_run: return issue_battle_order(side,Order::front);
    case BattleCommand::general_assault: return issue_battle_order(side,Order::general);
    case BattleCommand::retreat: return issue_battle_order(side,Order::retreat);
    case BattleCommand::front_back: return issue_battle_order(side,Order::front_back);
    case BattleCommand::disperse:
    case BattleCommand::fall_in: {
        // Do not restart a fractional row transition from its interpolated
        // position: that would make a 30-unit move end between grid rows.
        for(int i=side*200;i<(side+1)*200;++i) {
            const auto& soldier=battle.soldiers[i];
            if(soldier.hp>0 && soldier_in_front_rank(soldier) &&
               soldier.move_ticks && soldier.move_dy) {
                notice="前军正在调整，请稍候";
                return false;
            }
        }
        notice="";
        const int center_row=battle_commander_row;
        const int direction=side==0 ? 1 : -1;
        bool moved=false;
        for(int i=side*200;i<(side+1)*200;++i) {
            auto& soldier=battle.soldiers[i];
            if(soldier.hp<=0 || !soldier_in_front_rank(soldier)) continue;
            const int row=clamp((int(soldier.y)+battle_grid_row_height/2)/battle_grid_row_height,0,battle_grid_rows-1);
            int preferred_y=0;
            if(command==BattleCommand::disperse) {
                preferred_y=row<center_row ? -1 : row>center_row ? 1 : (soldier.index&1 ? 1 : -1);
            } else {
                preferred_y=row<center_row ? 1 : row>center_row ? -1 : 0;
            }
            if(!preferred_y) continue;
            const int offsets_x[3]={0,direction,-direction};
            for(int offset_x:offsets_x) {
                const int target_x=int(soldier.x)+offset_x*battle_grid_column_width;
                const int target_y=int(soldier.y)+preferred_y*battle_grid_row_height;
                if(target_x<0 || target_x>=battlefield_width || target_y<0 || target_y>battle_grid_max_y) continue;
                const int target_cell_x=(target_x+battle_grid_column_width/2)/battle_grid_column_width;
                const int target_cell_y=(target_y+battle_grid_row_height/2)/battle_grid_row_height;
                bool occupied=false;
                for(int other=0;other<400 && !occupied;++other) {
                    if(other==i) continue;
                    const auto& candidate=battle.soldiers[other];
                    if(candidate.hp<=0) continue;
                    const int candidate_x=candidate.move_ticks ? candidate.move_target_x : candidate.x;
                    const int candidate_y=candidate.move_ticks ? candidate.move_target_y : candidate.y;
                    occupied=(candidate_x+battle_grid_column_width/2)/battle_grid_column_width==target_cell_x &&
                             (candidate_y+battle_grid_row_height/2)/battle_grid_row_height==target_cell_y;
                }
                for(int other_side=0;other_side<2 && !occupied;++other_side) {
                    const auto& general=battle.sides[other_side];
                    occupied=general.hp>0 &&
                        (general.x+battle_grid_column_width/2)/battle_grid_column_width==target_cell_x &&
                        (battle_commander_y+battle_grid_row_height/2)/battle_grid_row_height==target_cell_y;
                }
                if(occupied) continue;
                soldier.move_target_x=int16_t(target_x);
                soldier.move_target_y=int16_t(target_y);
                soldier.move_dx=int8_t(offset_x);
                soldier.move_dy=int8_t(preferred_y);
                soldier.move_x_remainder=soldier.move_y_remainder=0;
                soldier.move_ticks=32;
                if(!battle.magic_delay_active(2+i)) soldier.action_ticks=0;
                moved=true;
                break;
            }
        }
        return moved;
    }
    default: return false;
    }
}

bool Game::cast_skill(int side, int slot) {
    if(side < 0 || side > 1 || slot < 0 || slot >= 4 || campaign.phase != Phase::battle || battle.pending_winner >= 0) return false;
    auto& f = battle.sides[side];
    const int unlocked = skill_count(f.general);
    if(slot >= unlocked) return false;
    return cast_battle_skill(side,skill_by_id(learned_skill(f.general,slot)));
}

bool Game::cast_ai_skill(int side,int table_slot) {
    if(side < 0 || side > 1 || table_slot < 0 || table_slot >= ::sg::skill_count ||
       campaign.phase != Phase::battle || battle.pending_winner >= 0) return false;
    const auto* skill = &skill_defs[table_slot];
    if(!skill->script) return false;
    return cast_battle_skill(side,skill);
}

void Game::auto_cast_skill(int side) {
    if(side<0 || side>1) return;
    const auto& fighter=battle.sides[side];
    if(fighter.hp<=0 || fighter.order==Order::retreat ||
       fighter.charge<original::cast_charge_max || battle.effect_remaining ||
       battle.row_setup_active || battle.paused || battle.pending_winner>=0) return;

    // demake GeneralController.IsCanReleaseMagic scans learned slots from 0,
    // stopping at the first empty slot, then CheckLeftGeneralMagicRelease
    // selects the highest affordable slot (3 down to 0).
    const int count=skill_count(fighter.general);
    for(int slot=0;slot<count;++slot) {
        const auto* skill=skill_by_id(learned_skill(fighter.general,slot));
        if(!skill || !skill->script) break;
        if(fighter.mp>=skill->mp) {
            for(int selected=count-1;selected>=0;--selected) {
                const auto* candidate=skill_by_id(learned_skill(fighter.general,selected));
                if(candidate && candidate->script && fighter.mp>=candidate->mp) {
                    cast_battle_skill(side,candidate);
                    return;
                }
            }
            return;
        }
    }
}

void demake_general_ai_tick(Game& game,int side,bool controlled) {
    auto& own=game.battle.sides[side];
    auto& countdown=game.battle.ai_decision_countdown[side];
    if(countdown) --countdown;
    if(!countdown) {
        countdown=original::battle_ai_decision_interval_updates;
        if(!controlled && own.hp>0 && own.order!=Order::retreat &&
           (own.ai_action_flags&fighter_opening_finished_flag)) game.auto_tactical_action(side);
    }
    if(!controlled) demake_general_opening_tick(game,side);
}

void demake_general_opening_tick(Game& game,int side) {
    auto& own=game.battle.sides[side];
    const auto& enemy=game.battle.sides[1-side];
    if((own.ai_action_flags&fighter_opening_finished_flag) || own.hp<=0 || own.order!=Order::hold || own.attack_ticks ||
       (game.battle.effect_remaining && game.battle.effect_side==side) ||
       game.battle.general_hit_ticks[side] || (own.ai_action_flags&fighter_attack_cooldown_flag)) return;
    unsigned timer=(own.ai_action_flags&fighter_idle_timer_mask)>>8;
    if(timer) --timer;
    own.ai_action_flags=(own.ai_action_flags&~fighter_idle_timer_mask)|(timer<<8);
    if(timer) return;
    if(!(own.ai_action_flags&fighter_opening_checked_flag)) {
        own.ai_action_flags|=fighter_opening_checked_flag;
        if(own.hp<20 && own.soldiers<10 && (enemy.hp>50 || enemy.soldiers>10) &&
           enemy.order!=Order::retreat) {
            own.order=Order::retreat;
            own.ai_action_flags|=fighter_opening_finished_flag;
        } else if(own.soldiers>0) {
            own.order=Order::advance;
            own.ai_action_flags|=fighter_opening_finished_flag;
        } else own.ai_action_flags|=uint32_t((2+game.roll(8))*30)<<8;
    } else {
        own.order=Order::general;
        own.ai_action_flags|=fighter_opening_finished_flag;
    }
}

void Game::auto_tactical_action(int side) {
    if(side < 0 || side > 1) return;
    auto& own = battle.sides[side];
    const auto& enemy = battle.sides[1-side];
    if(own.general < 0 || enemy.general < 0 || own.hp <= 0) return;

    const auto& officer = campaign.officers[own.general];
    const int own_force_class = is_ranged_force(own.force) ? 3 : 1;
    const int enemy_force_class = is_ranged_force(enemy.force) ? 3 : 1;
    const int formation_group = int(officer.formation) + (battle.layout_front[side][side ? battle.defender_index : battle.attacker_index] ? 7 : 0);
    const uint32_t mode_mask = formation_group >= 0 && formation_group < 32 ? 1u << formation_group : 0;
    const bool action3_used = own.ai_action_flags & 1;
    const bool action5_used = own.ai_action_flags & 2;
    const bool action6_used = own.ai_action_flags & 4;
    const int own_hp_ratio = own.initial_hp > 0 ? int(own.hp) * 10000 / own.initial_hp : 0;
    const int enemy_hp_ratio = enemy.initial_hp > 0 ? int(enemy.hp) * 10000 / enemy.initial_hp : 0;
    const int own_soldiers = maximum(0,int(own.soldiers));
    const int enemy_soldiers = maximum(0,int(enemy.soldiers));
    const int own_x_grid = clamp((int(own.x) - (battlefield_width/2 - 28*battle_grid_column_width) + battle_grid_column_width/2) / battle_grid_column_width,0,55);
    const int enemy_x_grid = clamp((int(enemy.x) - (battlefield_width/2 - 28*battle_grid_column_width) + battle_grid_column_width/2) / battle_grid_column_width,0,55);
    bool own_cells[battle_grid_rows][battle_grid_columns]{};
    auto mark_own_cell = [&](int x,int y) {
        const int column=clamp((x-(battlefield_width/2-28*battle_grid_column_width)+battle_grid_column_width/2)/battle_grid_column_width,0,battle_grid_columns-1);
        const int row=clamp((y+battle_grid_row_height/2)/battle_grid_row_height,0,battle_grid_rows-1);
        own_cells[row][column]=true;
    };
    mark_own_cell(own.x,battle_commander_y);
    for(int i=side*200;i<(side+1)*200;++i)
        if(battle.soldiers[i].hp>0) mark_own_cell(battle.soldiers[i].x,battle.soldiers[i].y);
    int own_cells_between=0;
    for(int column=minimum(own_x_grid,enemy_x_grid);column<=maximum(own_x_grid,enemy_x_grid);++column)
        own_cells_between += own_cells[battle_commander_row][column] ? 1 : 0;

    int weights[7] = {100,0,0,0,0,0,0};
    int command_candidates[3] = {1,0,0};
    int command_count = 1;
    if(mode_mask == 0x10) {
        command_candidates[1] = 4; command_candidates[2] = 3; command_count = 3;
    } else if(mode_mask == 0x20) {
        command_candidates[1] = 5; command_candidates[2] = 2; command_count = 3;
    }
    bool command_ready = false;
    for(int i=0;i<command_count;++i)
        if(own.ai_command_slots[command_candidates[i]] == 0) command_ready = true;

    if(own_soldiers > 0 && !action3_used && !action6_used && command_ready) {
        int weight = 12;
        if(mode_mask == 4) weight += 150;
        if(enemy_force_class == 3 && own_force_class != 3) weight += 1000;
        if(enemy_soldiers <= own_soldiers) weight += 6 * (own_soldiers-enemy_soldiers);
        if(own_soldiers > 50) weight += own_soldiers-50;
        if(own_force_class == 3 && enemy_force_class != 3) weight = maximum(0,weight-100);
        if(enemy_force_class == 3 && own_cells_between < 2) weight += 100;
        if(enemy.ai_action_flags & 4) weight += 10000;
        if(action5_used) weight += 1000;
        if((own_x_grid > enemy_x_grid ? own_x_grid-enemy_x_grid : enemy_x_grid-own_x_grid) < 4) weight += 300;
        weights[2] = weight;
    }

    if(own_soldiers > 0 && !action3_used && !action6_used) {
        int weight = 0;
        if(mode_mask == 8) weight += 60;
        if(enemy_soldiers > 2*own_soldiers) weight += 2*(enemy_soldiers-own_soldiers);
        if(own_force_class == 3) weight += 240;
        weights[3] = weight;
    }

    if(!action6_used && !battle.effect_remaining && own.charge >= original::cast_charge_max) {
        bool affordable_skill = false;
        bool damaging_skill = false, soldier_skill = false, healing_skill = false, reinforcement_skill = false;
        for(int slot=0;slot<skill_count(own.general);++slot) {
            const auto* skill=skill_by_id(learned_skill(own.general,slot));
            if(!skill || !skill->script || own.mp<skill->mp) continue;
            affordable_skill = true;
            damaging_skill |= (skill->attrib & (0x10|0x40)) != 0;
            soldier_skill |= (skill->attrib & 0x0f) != 0;
            healing_skill |= (skill->attrib & 0x100) != 0;
            reinforcement_skill |= (skill->attrib & 0x200) != 0;
        }
        if(affordable_skill) {
            int weight = 0;
            if(damaging_skill) {
                if(enemy_hp_ratio < 825) weight += 400;
                else if(enemy_hp_ratio < 1600) weight += 160;
                else if(enemy_hp_ratio < 2400) weight += 80;
                else if(enemy_hp_ratio < 3300) weight += 50;
                weight += 30;
            }
            if(soldier_skill) {
                weight += 6;
                if(enemy.ai_action_flags & 1) weight += 30;
                weight += 6*enemy_soldiers;
            }
            if(healing_skill) {
                if(own_hp_ratio < 825) weight += 200;
                else if(own_hp_ratio < 1600) weight += 60;
                else if(own_hp_ratio < 2400) weight += 30;
                else if(own_hp_ratio < 3300) weight += 12;
            }
            if(reinforcement_skill && own.initial_soldiers-own_soldiers > 10)
                weight += 2*(own.initial_soldiers-own_soldiers);
            weights[4] = weight;
        }
    }

    if(!action5_used && !action6_used) {
        int weight = 0;
        if(enemy_soldiers < 20 && (own.ai_action_flags & 1) &&
           uint16_t(battle.tick-uint16_t(own.ai_last_action_tick)) > 300 &&
           int(enemy.hp)*strength(enemy.general) < int(own.hp)*strength(own.general)+500)
            weight += 8;
        if(enemy_soldiers < 5 && own_soldiers < 5) {
            weight += 3;
            if(int(enemy.hp)*strength(enemy.general) < int(own.hp)*strength(own.general)-200)
                weight += 15;
        }
        if(own_soldiers < 3) {
            weight += 50-enemy_soldiers/2;
            if(own_hp_ratio > 9000) weight += 40;
            else if(own_hp_ratio > 8000) weight += 20;
            else if(own_hp_ratio > 5000) weight += 10;
        }
        if(enemy_hp_ratio < 825) weight += 200;
        else if(enemy_hp_ratio < 1600) weight += 60;
        else if(enemy_hp_ratio < 2400) weight += 50;
        else if(enemy_hp_ratio < 3300) weight += 30;
        else if(enemy_hp_ratio < 5000) weight += 10;
        if(own.initial_hp > 0) weight = weight*own.hp/own.initial_hp;
        if(enemy.ai_action_flags & 4) weight += 10000;
        weights[5] = maximum(0,weight);
    }

    // Keep demake's delayed opening, but do not make its first decision a
    // lifetime ban on retreat in the GBA's native tactical AI. Reconsider only
    // after taking damage THIS bout, with few troops and a strong opponent.
    // An unchanged, low-health re-entry must not immediately flee again.
    const bool combat_retreat=(own.ai_action_flags&fighter_opening_checked_flag) &&
        (own.ai_action_flags&fighter_opening_finished_flag) &&
        own.hp<20 && own.hp<battle.starting_hp[side] && own_soldiers<10 && enemy.hp>0 &&
        (enemy.hp>50 || enemy_soldiers>10) && enemy.order!=Order::retreat;
    if(!action6_used && !(enemy.ai_action_flags & 4) &&
       (combat_retreat || (!(own.ai_action_flags&fighter_opening_checked_flag) &&
                           own_soldiers == 0 && enemy_soldiers > 0))) {
        int weight = 0;
        if(enemy_force_class == 3 && own_hp_ratio < 5000 && enemy_hp_ratio > 8000) weight += 30;
        else if(enemy_force_class != 3 && own_hp_ratio < 5000 && enemy_soldiers > 19) weight += enemy_soldiers/3;
        if(own_hp_ratio < 825) weight += 60;
        else if(own_hp_ratio < 1600) weight += 30;
        else if(own_hp_ratio < 2400) weight += 15;
        else if(own_hp_ratio < 3300) weight += 6;
        if(enemy_hp_ratio < 825) weight -= 50;
        else if(enemy_hp_ratio < 1600) weight -= 25;
        else if(enemy_hp_ratio < 2400) weight -= 12;
        else if(enemy_hp_ratio < 3300) weight -= 4;
        weight = maximum(0,weight);
        if(own_hp_ratio > 3300) weight = weight*own.hp/own.initial_hp;
        // Absolute danger must still matter when the bout started wounded,
        // and in a general-only duel with no enemy soldiers left.
        if(combat_retreat) weight=maximum(weight,100);
        weights[6] = weight;
    }

    int total = 0;
    for(int weight : weights) total += weight;
    if(total <= 0) return;
    int draw = roll(total);
    int action = 0;
    for(;action<7;++action) {
        if(draw < weights[action]) break;
        draw -= weights[action];
    }
    if(action == 2) {
        int selected = -1;
        if(mode_mask == 0x10 && own.ai_command_slots[1] != 0) {
            const int first = (roll(2) == 0) ? 4 : 3;
            const int second = first == 4 ? 3 : 4;
            if(own.ai_command_slots[first] == 0) selected = first;
            else if(own.ai_command_slots[second] == 0) selected = second;
        } else {
            for(int i=0;i<command_count;++i)
                if(own.ai_command_slots[command_candidates[i]] == 0) { selected=command_candidates[i]; break; }
        }
        if(selected > 0) own.ai_command_slots[selected] = 1;
    } else if(action == 3) {
        own.ai_action_flags |= 1;
        for(int slot=1;slot<8;++slot) own.ai_command_slots[slot] = 7;
        own.ai_last_action_tick = battle.tick;
    } else if(action == 4) {
        // Direct demake-style release normally already cast as soon as the
        // meter filled; retain this fallback if the action draw coincides.
        auto_cast_skill(side);
    } else if(action == 5) {
        own.ai_command_slots[0] = 1;
        own.ai_action_flags |= 2;
    } else if(action == 6) {
        for(int slot=0;slot<8;++slot) own.ai_command_slots[slot] = 8;
        own.ai_action_flags |= 4;
        own.order = Order::retreat;
    }
}

bool Game::cast_battle_skill(int side,const SkillDef* skill) {
    if(side < 0 || side > 1 || campaign.phase != Phase::battle || battle.pending_winner >= 0 ||
       battle.row_setup_active || battle.paused) return false;
    auto& f = battle.sides[side];
    if(f.hp <= 0 || (f.order==Order::retreat && !battle.magic_composite_bootstrap) ||
       !skill || !skill->script || f.mp < skill->mp ||
       f.charge < original::cast_charge_max ||
       (battle.effect_remaining && !battle.magic_composite_bootstrap)) return false;
    const bool composite_root=skill->script==2663 || skill->script==2664;
    if(composite_root && battle.magic_composite_kind) return false;
    const int composite_slots=original::composite_projectile_reservation(skill->script);
    if(composite_slots) {
        int free=0;for(const auto& p:battle.projectiles) free+=!p.active;
        if(free<composite_slots) {notice="战场效果数量已达上限";return false;}
    }
    const int arrow_count = original::arrow_volley_count(skill->script);
    const int firewheel_count = original::fire_wheel_object_count(skill->script);
    const int rockfall_count = original::rockfall_strike_count(skill->script);
    const bool rockfall_fire=skill->script==2622 || skill->script==2623;
    const int rockfall_fire_visuals=rockfall_fire ? 12 : 0;
    const int mine_count = original::minefield_count(skill->script);
    const int sword_wave_count = original::sword_wave_projectile_count(skill->script);
    const int m014_child_token_count = skill->script>=2639 && skill->script<=2641 ? 30 : 0;
    const int heaven_beam_count = original::heaven_and_earth_beam_count(skill->script);
    const int heaven_strike_count = original::heaven_and_earth_general_strike_count(skill->script);
    const int ghost_projectile_limit = original::ghost_crying_god_projectile_count(skill->script);
    const int ghost_visual_count = original::ghost_crying_god_visual_count(skill->script);
    const int ghost_siege_visual_count = original::ghost_crying_god_siege_visual_count(skill->script);
    const int ghost_grid_count = original::ghost_crying_god_grid_count(skill->script);
    const int chariot_count = original::chariot_visual_count(skill->script);
    const int chariot_object_count = original::chariot_collision_object_count(skill->script);
    const int magic_field_objects = original::magic_field_objects_per_iteration(skill->script);
    const int magic_field_visual_objects = original::magic_field_visual_objects_per_iteration(skill->script);
    int magic_field_iterations = original::magic_field_iteration_count(skill->script);
    const int reinforcement_count = original::reinforcement_requested_count(
        skill->script,level(f.general),int(f.soldiers));
    uint8_t magic_field_state = 0;
    if(original::reinforcement_amount(skill->script) && reinforcement_count==0)
        magic_field_state=1; // GLOBAL6400 takes its zero-capacity M024Fail branch.
    if(reinforcement_count>0) {
        bool free=false;
        for(const auto& p:battle.projectiles) if(!p.active) {free=true;break;}
        if(!free) {notice="战场效果数量已达上限";return false;}
    }
    if(skill->script == 2665) {
        // demake checks the commanders' local-position distance (<200), then
        // damages only the opposing general in four callbacks.  The GBA
        // battlefield uses the same world-unit scale as the other battle
        // movement code, so 200 demake units maps to about five 40-unit cells.
        const auto& enemy=battle.sides[1-side];
        int distance=int(f.x)-int(enemy.x);
        if(distance<0) distance=-distance;
        const bool close=distance < 200;
        magic_field_state=close ? 1 : 0;
        magic_field_iterations=close ? 4 : 1;
    }
    const int magic_field_visual_total = skill->script==2665 ?
        magic_field_iterations :
        magic_field_visual_objects*magic_field_iterations;
    const int stationary_callback_count = original::stationary_callback_object_count(skill->script);
    const int yellow_dragon_flame_count = skill->script==2617 ? 10 : 0;
    const int yellow_dragon_fire_count = skill->script==2617 ? 10 : 0;
    int yellow_dragon_flame_start=215;
    int yellow_dragon_flame_waits=0;
    const bool tornado = original::magic_uses_tornado(skill->script);
    int enemy_soldiers[200];
    int enemy_soldier_count = 0;
    if(arrow_count) {
        for(int i = 0; i < 200; ++i) if(battle.soldiers[(1-side)*200+i].hp > 0)
            enemy_soldiers[enemy_soldier_count++] = i;
    }
    if(firewheel_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < firewheel_count) { notice = "战场效果数量已达上限"; return false; }
    }
    if(chariot_object_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < chariot_object_count) {
            notice = "战场效果数量已达上限";
            return false;
        }
    }
    if(rockfall_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < rockfall_count * (3+rockfall_fire_visuals)) {
            notice = "战场效果数量已达上限"; return false;
        }
    }
    if(arrow_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        const int arrow_visuals_per_shot=skill->script>=2603 && skill->script<=2605 ? 3 :
            skill->script>=2600 && skill->script<=2602 ? 2 : 0;
        if(free_projectiles < arrow_count*(1+arrow_visuals_per_shot)) {
            notice = "战场效果数量已达上限"; return false;
        }
    }
    if(mine_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < mine_count) { notice = "战场效果数量已达上限"; return false; }
    }
    if(sword_wave_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < sword_wave_count) { notice = "战场效果数量已达上限"; return false; }
    }
    if(m014_child_token_count) {
        int free_projectiles=0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < m014_child_token_count+sword_wave_count+13) {
            notice="战场效果数量已达上限"; return false;
        }
    }
    if(heaven_beam_count || heaven_strike_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < heaven_beam_count + heaven_strike_count) {
            notice = "战场效果数量已达上限"; return false;
        }
    }
    if(stationary_callback_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < stationary_callback_count) { notice = "战场效果数量已达上限"; return false; }
    }
    if(yellow_dragon_flame_count) {
        int free_projectiles=0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles<yellow_dragon_flame_count+yellow_dragon_fire_count) {
            notice="战场效果数量已达上限";
            return false;
        }
    }
    const int ghost_projectile_count = ghost_projectile_limit;
    if(ghost_projectile_count) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        // GLOBAL 6202 contributes one sequence-4600 collision object.
        const int ghost_clone_projectiles = original::magic_uses_ghost_crying_god(skill->script) ? 1 : 0;
        if(free_projectiles < ghost_projectile_count + ghost_clone_projectiles + ghost_visual_count) { notice = "战场效果数量已达上限"; return false; }
    }
    if(magic_field_objects) {
        int free_projectiles = 0;
        for(const auto& projectile : battle.projectiles) if(!projectile.active) ++free_projectiles;
        if(free_projectiles < magic_field_objects*magic_field_iterations + magic_field_visual_total) {
            notice = "战场效果数量已达上限";
            return false;
        }
    }
    Projectile* magic_projectiles[3]{};
    const int moving_projectile_count = tornado ? original::tornado_count(skill->script) : original::dragon_projectile_count(skill->script) ?
        original::dragon_projectile_count(skill->script) :
        original::magic_uses_collision_projectile(skill->script) || tornado ||
        original::magic_uses_ghost_crying_god(skill->script) ? 1 : 0;
    const int m011_row_visual_count = skill->script == 2631 ? 2 :
        skill->script == 2632 ? 4 : 0;
    Projectile* m011_row_visuals[4]{};
    if(m011_row_visual_count) {
        int reserved=0;
        const int needed=moving_projectile_count+m011_row_visual_count;
        for(auto& projectile : battle.projectiles)
            if(!projectile.active && reserved<needed) ++reserved;
        if(reserved<needed) { notice = "战场效果数量已达上限"; return false; }
    }
    if(moving_projectile_count) {
        int reserved=0;
        for(auto& projectile : battle.projectiles) if(!projectile.active && reserved<moving_projectile_count)
            magic_projectiles[reserved++]=&projectile;
        if(reserved<moving_projectile_count) { notice = "战场效果数量已达上限"; return false; }
    }
    if(m011_row_visual_count) {
        int reserved=0;
        for(auto& projectile : battle.projectiles) if(!projectile.active &&
            reserved<m011_row_visual_count) {
            bool used_for_moving=false;
            for(int i=0;i<moving_projectile_count;++i)
                if(magic_projectiles[i]==&projectile) used_for_moving=true;
            if(!used_for_moving) m011_row_visuals[reserved++]=&projectile;
        }
        if(reserved<m011_row_visual_count) { notice = "战场效果数量已达上限"; return false; }
    }
    f.mp = int16_t(f.mp - skill->mp);
    // Native FUN_0043cd6d spends the officer's persistent MP directly.
    campaign.officers[f.general].mp = uint16_t(f.mp);
    // demake's SetIsSwordFull(false) resets the full sword meter to zero for
    // every skill, regardless of that skill's POWER value.
    f.charge = 0;
    // Entering Magic replaces the attack animation; its pending melee event
    // must not fire invisibly underneath the casting pose.
    f.attack_ticks=0;
    battle.cast_pose_start_tick = battle.tick;
    if(composite_root) {
        int reserved=0;
        for(auto& p:battle.projectiles) if(!p.active && reserved<composite_slots) {
            p={};p.active=1;p.magic=37;p.target_side=int8_t(side);
            p.source_script=int16_t(skill->script);++reserved;
        }
        battle.magic_composite_kind=uint8_t(skill->script==2663 ? 1 : 2);
        battle.magic_composite_stage=0;
        battle.magic_composite_side=uint8_t(side);
        battle.magic_composite_bootstrap=1;
        // The demake composition wrapper keeps the caster busy across all three
        // child prefabs. The first two transitions match its WaitForSeconds
        // cadence at the GBA 30 Hz battle update rate.
        battle.magic_composite_wait=0;
        battle.effect_remaining=1;
        battle.effect_ticks=0;
        battle.effect_side=uint8_t(side);
        battle.effect_kind=0;
        battle.effect_script=int16_t(skill->script);
        battle.effect_applied=1;
        battle.magic_field_center_x=f.x;
        battle.magic_field_center_y=battle_commander_y;
        battle.magic_field_origin_x=battle.sides[1-side].x;
        battle.magic_field_origin_y=battle_commander_y;
        battle.magic_callback_damage=0;
        battle.magic_field_iterations=battle.magic_field_wait=battle.magic_field_state=0;
        battle.heal_wait=battle.heal_remainder_wait=battle.heal_visual_remaining=0;
        battle.heal_steps=battle.heal_remainder=0;
        notice = skill->name;
        return true;
    }
    const int stationary_windup = original::stationary_callback_windup(skill->script);
    battle.effect_remaining = uint16_t(stationary_windup ? stationary_windup :
        original::magic_busy_duration(skill->script,magic_field_iterations,
                                       f.x,side,battlefield_width,
                                       reinforcement_count));
    battle.immunity_ticks[side] = uint16_t(
        original::magic_immunity_duration(skill->script,reinforcement_count));
    battle.effect_ticks = uint16_t(stationary_windup ? stationary_windup :
        original::magic_effect_launch_delay(skill->script));
    battle.effect_side = uint8_t(side);
    battle.effect_kind = skill->effect;
    battle.effect_script = int16_t(skill->script);
    // ShenGuiLuanWu resolves entirely through its four demake-style general
    // callbacks; do not let the generic effect resolver apply a second hit.
    battle.effect_applied = skill->script==2665 ? 1 : 0;
    battle.magic_callback_damage = uint8_t(clamp(
        skill->script==2665 ? (magic_field_state ? 20 : 0) :
            original::magic_general_hit_bonus(skill->script),0,255));
    battle.magic_field_iterations = uint8_t(magic_field_iterations);
    battle.magic_field_wait = magic_field_iterations ?
        uint8_t(original::magic_field_launch_delay(skill->script)) : 0;
    if(skill->script>=2639 && skill->script<=2641)
        magic_field_state=2; // Capture native269's target at GLOBAL4420 start.
    battle.magic_field_state = magic_field_state;
    battle.magic_field_center_x = f.x;
    battle.magic_field_center_y = battle_commander_y;
    if(skill->script==2665)
        battle.magic_field_center_x=battle.sides[1-side].x;
    if(skill->script==2617) {
        // MAGIC01 3601 begins with RNG(10,50) before creating the dragon.
        // Sample a private copy so the visual wait varies without perturbing
        // combat RNG; concurrent source-script interleaving remains approximate.
        uint32_t visual_primary=campaign.random;
        uint32_t visual_secondary=campaign.random_secondary;
        if(!visual_primary && !visual_secondary) {
            visual_primary=original::random_initial_primary;
            visual_secondary=original::random_initial_secondary;
        }
        const uint32_t next_primary=visual_primary+1;
        const uint32_t previous_secondary=visual_secondary-1;
        const unsigned secondary_shift=next_primary&31;
        visual_secondary=(previous_secondary>>secondary_shift)+
            (previous_secondary<<((32-secondary_shift)&31));
        const unsigned primary_shift=visual_secondary&31;
        visual_primary=(next_primary<<primary_shift)+
            (next_primary>>((32-primary_shift)&31));
        battle.magic_field_center_x=int16_t(10+
            (visual_primary+visual_secondary)%41);
    }
    battle.magic_field_origin_x = f.x;
    battle.magic_field_origin_y = battle_commander_y;
    if(firewheel_count) {
        int reserved=0;
        for(int angle_step=7; angle_step>=0 && reserved<firewheel_count; --angle_step) {
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                projectile={};
                projectile.active=1;
                projectile.magic=1;
                projectile.kind=skill->script==2608 ? 2 : 3;
                projectile.phase=0;
                projectile.collision_enabled=1;
                projectile.target_side=int8_t(1-side);
                projectile.source_script=int16_t(skill->script);
                projectile.target_offset_x=f.x; // saved orbit center
                projectile.height=battle_commander_y;
                projectile.x=f.x;
                projectile.y=battle_commander_y;
                projectile.scatter=uint8_t(angle_step*32);
                // The angle is native17 facing; MAGIC01's real pre-spawn
                // wait is20 updates and is shared by all eight children.
                projectile.remaining=20;
                projectile.owner_index=uint8_t(original::fire_wheel_active_updates);
                ++reserved;
                break;
            }
        }
    }
    if(magic_field_objects) {
        const int collision_count=magic_field_objects*magic_field_iterations;
        const int visual_count=magic_field_visual_total;
        int reserved=0;
        for(auto& projectile : battle.projectiles) if(!projectile.active && reserved<collision_count) {
            projectile={};
            projectile.active=1;
            projectile.magic=10;
            projectile.phase=2; // reserved for the field's delayed bursts
            projectile.target_side=int8_t(1-side);
            projectile.source_script=int16_t(skill->script);
            ++reserved;
        }
        reserved=0;
        for(auto& projectile : battle.projectiles) if(!projectile.active && reserved<visual_count) {
            projectile={};
            projectile.active=1;
            projectile.magic=13;
            projectile.phase=2; // reserved for original M020 visual children
            projectile.source_script=int16_t(skill->script);
            projectile.owner_index=uint8_t(reserved);
            ++reserved;
        }
    }
    if(rockfall_count) {
        int reserved = 0;
        for(auto& projectile : battle.projectiles) if(!projectile.active && reserved < rockfall_count) {
            projectile = {};
            projectile.active = 1;
            projectile.magic = 11;
            projectile.phase = 2; // held until GLOBAL3700 finishes its setup.
            projectile.target_side = int8_t(1-side);
            projectile.source_script = int16_t(skill->script);
            projectile.kind = uint8_t(reserved); // Stable strike index for impact children.
            projectile.target_offset_x = int16_t(battle.tick); // Cast key for reserved visuals.
            ++reserved;
        }
        for(int lane=0;lane<rockfall_count;++lane) {
            const bool second_lane=skill->script==2623 && lane>=7;
            const int lane_index=second_lane ? lane-7 : lane;
            int local0=0;
            int native_y=0;
            if(skill->script==2618) {
                local0=4-lane_index*2;
                native_y=36+local0*144;
            } else if(!second_lane) {
                local0=6-lane_index;
                native_y=36+local0*144;
            } else {
                local0=5-lane_index;
                native_y=108+local0*144;
            }
            const int native_x=second_lane ? (side==0 ? 5984 : -624) :
                                                (side==0 ? 5792 : -432);
            // MAGIC01 3700 creates the short sequence3701 marker immediately,
            // then starts a delayed child that creates the separate animated
            // sequence3700 cart. They can overlap for up to nine updates.
            for(int marker=0;marker<2;++marker) {
                Projectile* visual=nullptr;
                for(auto& projectile : battle.projectiles) if(!projectile.active) {
                    visual=&projectile;
                    break;
                }
                if(!visual) break;
                *visual={};
                visual->active=1;
                visual->magic=15;
                visual->phase=2; // GLOBAL3700's shared 20+30 setup.
                visual->source_script=int16_t(skill->script);
                visual->target_side=int8_t(side);
                visual->owner_index=uint8_t(lane);
                visual->kind=uint8_t((second_lane ? 1 : 0) | (marker ? 2 : 0));
                visual->x=int16_t(clamp(native_x*battle_grid_column_width/96,
                                         0,battlefield_width-1));
                visual->y=int16_t(clamp(native_y*battle_grid_row_height/72,
                                         0,battle_grid_max_y));
                visual->remaining=50;
            }
            if(rockfall_fire) {
                // Fire stones (GLOBAL var20=1) create one sequence3801 impact
                // and ten persistent 3810-3814 field sprites after death.
                // Reserve all twelve tokens now so concurrent effects cannot
                // take their slots before a delayed stone lands.
                for(int effect=0;effect<12;++effect) {
                    Projectile* visual=nullptr;
                    for(auto& projectile : battle.projectiles) if(!projectile.active) {
                        visual=&projectile;
                        break;
                    }
                    if(!visual) break;
                    *visual={};
                    visual->active=1;
                    visual->magic=uint8_t(effect==0 ? 28 : effect==1 ? 29 : 30);
                    visual->phase=2;
                    visual->source_script=int16_t(skill->script);
                    visual->target_side=int8_t(1-side);
                    visual->owner_index=uint8_t(lane);
                    visual->kind=uint8_t(lane);
                    visual->target_offset_x=int16_t(battle.tick);
                }
            }
        }
    }
    const int heal_amount = original::healing_amount(skill->script);
    battle.heal_wait = heal_amount ? 35 : 0;
    battle.heal_remainder_wait = 0;
    // Three particles are emitted at updates35..94 and live32 updates.
    // Keep the final source objects visible through update125 even though
    // the root cast lock ends at update125.
    battle.heal_visual_remaining = heal_amount ? 126 :
        (original::reinforcement_amount(skill->script) && !reinforcement_count ? 68 : 0);
    battle.heal_steps = heal_amount ? 15 : 0;
    battle.heal_remainder = heal_amount > 15 ? uint8_t(heal_amount - 15) : 0;
    bool cyclone_general_targeted=false;
    for(int i=0;i<moving_projectile_count;++i) {
        auto& projectile=*magic_projectiles[i];
        projectile = {};
        projectile.active = 1;
        projectile.magic = tornado ? 5 : 1;
        projectile.collision_enabled = uint8_t(!tornado);
        projectile.x = f.x;
        projectile.y = battle_commander_y;
        projectile.dx = int16_t(side ? 3 : -3);
        // Launch timing comes from each wrapper: 4100 wave=15+10,
        // 4200 family=15, and MAGIC01 4500 dragon=20+30 updates.
        projectile.phase = 0;
        projectile.remaining = uint8_t(original::magic_projectile_launch_delay(skill->script)+
            (i ? i*original::dragon_projectile_interval(skill->script) : 0));
        projectile.target_side = int8_t(1-side);
        projectile.owner_index = uint8_t(original::dragon_projectile_count(skill->script) ? i : 0xff);
        projectile.source_script = int16_t(skill->script);
        if(original::dragon_projectile_count(skill->script)) {
            // demake MagicFireDragon.Init/GetArmyCentrePoint: mean of live
            // enemy soldiers, falling back to its general for an empty army.
            int sum=0,count=0;
            for(int index=0;index<200;++index) {
                const auto& soldier=battle.soldiers[(1-side)*200+index];
                if(soldier.hp>0) { sum+=soldier.x; ++count; }
            }
            const int center=count ? sum/count : battle.sides[1-side].x;
            const int direction=side ? 1 : -1;
            const int lanes=original::dragon_projectile_count(skill->script);
            projectile.target_offset_x=int16_t(center);
            projectile.x=int16_t(center-direction*100);
            projectile.y=int16_t(battle_commander_y+(2*i-(lanes-1))*30);
            projectile.remaining=24;
            projectile.scatter=uint8_t(lanes);
        }
        if(skill->script>=2630 && skill->script<=2632) {
            // GLOBAL4100 calls native21 with radius64 and angle128 for side0,
            // angle0 for side1 before spawning the independent 4101 rows.
            projectile.x=int16_t(clamp(int(projectile.x)+
                (side==0 ? -64 : 64)*battle_grid_column_width/96,
                0,battlefield_width-1));
        }
        // The main 2664 child uses sequence 4600, while its other children
        // are arrows that share the same source script ID.
        if(original::magic_uses_ghost_crying_god(skill->script)) {
            projectile.kind = 1;
            // GLOBAL 6202 is started only after GLOBAL 6200 finishes its
            // 110-update arrow setup. Its own 15+10 waits follow that.
            projectile.remaining = 110;
            // GLOBAL 6202 starts local5=16 and local4=1, then advances them
            // together once per update while local5 < 24*16.
            projectile.target_offset_x = 16;
            projectile.owner_index = 1;
        }
        if(skill->script>=2645 && skill->script<=2647) {
            // GLOBAL4600 starts local5=16/local4=1, then increases native
            // speed by 1..24 pixels per update. scatter stores local4 so the
            // collision owner slot remains available to the hit resolver.
            projectile.target_offset_x = 16;
            projectile.scatter = 1;
            // Keep the visual RNG draws, but do not turn the source's depth
            // variation into a different battlefield collision lane. demake's
            // MagicFenShenZhan.GenerateMan starts on the commander's line;
            // its tiny per-copy y offset is only depth sorting.
            if(roll(4)==3) {
                (void)roll(91);
            }
        }
        if(tornado) {
            int count=0,sum_x=0,sum_y=0;
            for(int index=0;index<200;++index) {
                const auto& soldier=battle.soldiers[(1-side)*200+index];
                if(soldier.hp>0) {++count;sum_x+=soldier.x;sum_y+=soldier.y;}
            }
            int selected=-1;
            if(count) {
                int rank=roll(count);
                for(int index=0;index<200;++index)
                    if(battle.soldiers[(1-side)*200+index].hp>0 && rank--==0) {selected=index;break;}
            }
            bool duplicate=false;
            for(int previous=0;previous<i;++previous)
                if(selected>=0 && magic_projectiles[previous]->kind==selected) duplicate=true;
            if(selected>=0 && !duplicate) {
                const auto& target=battle.soldiers[(1-side)*200+selected];
                projectile.x=target.x;projectile.y=target.y;
            } else if(!cyclone_general_targeted && roll(100)<40) {
                cyclone_general_targeted=true;
                projectile.x=battle.sides[1-side].x;
                projectile.y=battle_commander_y;
            } else {
                const int center_x=count ? sum_x/count : battle.sides[1-side].x;
                const int center_y=count ? sum_y/count : battle_commander_y;
                projectile.x=int16_t(clamp(center_x+roll(360)-180,0,battlefield_width-1));
                projectile.y=int16_t(clamp(center_y+roll(200)-100,0,battle_grid_max_y));
            }
            projectile.kind=uint8_t(selected>=0 && !duplicate ? selected : 255);
            projectile.owner_index=uint8_t(i);
            projectile.scatter=uint8_t(moving_projectile_count);
            projectile.remaining=uint8_t(24+18*i);
            projectile.collision_enabled=1;
        }
    }
    if(m011_row_visual_count) {
        static constexpr int16_t row_native_x[4]={48,48,208,208};
        static constexpr int16_t row_native_y[4]={140,280,140,280};
        const int side_sign=side==0 ? 1 : -1;
        for(int i=0;i<m011_row_visual_count;++i) {
            auto& visual=*m011_row_visuals[i];
            visual={};
            visual.active=1;
            visual.magic=27; // Static sequence4101 child, separate from 4100.
            visual.phase=0;
            visual.remaining=uint8_t(original::magic_projectile_launch_delay(skill->script));
            visual.source_script=int16_t(skill->script);
            visual.target_side=int8_t(1-side);
            visual.dx=int16_t(side ? 3 : -3);
            const int parent_native_x=int(f.x)+(side==0 ? -64 : 64)*battle_grid_column_width/96;
            visual.x=int16_t(clamp(parent_native_x+
                row_native_x[i]*battle_grid_column_width/96,0,battlefield_width-1));
            visual.y=int16_t(clamp(battle_commander_y+18+
                side_sign*row_native_y[i]*battle_grid_row_height/72,0,battle_grid_max_y));
            // Sequences4101 hold the two original B frames at #2 and have no
            // short child timer; keep the static children for this battle.
        }
    }
    if(ghost_visual_count) {
        int reserved=0;
        const int native_x=side==0 ? 5984 : -624; // GLOBAL6201's G5==0 / !=0 lanes.
        const int native_field_width=battle_grid_columns*96;
        const int visual_x=native_x<0 ? (-native_x)*battle_grid_column_width/96 :
            native_x>=native_field_width ? battlefield_width-
                (native_x-native_field_width)*battle_grid_column_width/96 :
                native_x*battle_grid_column_width/96;
        for(auto& projectile : battle.projectiles) if(!projectile.active && reserved<ghost_siege_visual_count) {
            projectile={};
            projectile.active=1;
            projectile.magic=15;
            projectile.phase=2; // GLOBAL6201's 50-update setup.
            projectile.source_script=int16_t(skill->script);
            projectile.target_side=int8_t(side);
            projectile.owner_index=uint8_t(reserved);
            projectile.x=int16_t(visual_x);
            if(reserved==0) {
                projectile.y=int16_t(180*battle_grid_row_height/72); // final position of the moving 3701 marker.
            } else {
                const int local0=6-reserved; // Six MAGIC01 3701 children: 5..0.
                const int native_y=180+144*local0;
                projectile.y=int16_t(native_y*battle_grid_row_height/72);
            }
            projectile.remaining=50;
            ++reserved;
        }
        for(int i=0;i<ghost_grid_count;++i) {
            const int native_row=9-(i%10);
            const int native_col=i/10;
            const int grid_native_x=side==0 ? 5792+native_col*96 : -432-native_col*96;
            const int native_y=180+native_row*72;
            const int grid_field_width=battle_grid_columns*96;
            const int world_x=grid_native_x<0 ? (-grid_native_x)*battle_grid_column_width/96 :
                grid_native_x>=grid_field_width ? battlefield_width-
                    (grid_native_x-grid_field_width)*battle_grid_column_width/96 :
                    grid_native_x*battle_grid_column_width/96;
            auto reserve_visual=[&](int magic,int fire_offset) {
                Projectile* visual=nullptr;
                for(auto& candidate : battle.projectiles) if(!candidate.active) {
                    visual=&candidate;
                    break;
                }
                if(!visual) return;
                *visual={};
                visual->active=1;
                visual->magic=uint8_t(magic);
                visual->phase=0;
                visual->remaining=50; // GLOBAL 6200's setup before the grid effects.
                visual->source_script=int16_t(skill->script);
                visual->target_side=int8_t(side);
                visual->owner_index=uint8_t(i);
                visual->kind=uint8_t(side!=0);
                const int offset_x=side==0 ? -fire_offset : fire_offset;
                const int visual_native_x=grid_native_x+offset_x;
                const int visual_world_x=visual_native_x<0 ?
                    (-visual_native_x)*battle_grid_column_width/96 :
                    visual_native_x>=grid_field_width ? battlefield_width-
                    (visual_native_x-grid_field_width)*battle_grid_column_width/96 :
                    visual_native_x*battle_grid_column_width/96;
                visual->x=int16_t(clamp(magic==19 ? visual_world_x : world_x,0,battlefield_width-1));
                visual->y=int16_t(native_y*battle_grid_row_height/72);
            };
                reserve_visual(18,0); // M001MANA1/MANB1 10-update placeholder.
                reserve_visual(26,0); // Separate delayed sequence3000/3001 child.
            reserve_visual(19,27); // M002 ArrowFire3 at the source's +/-27 offset.
        }
    }
    if(stationary_callback_count) {
        int reserved = 0;
        for(auto& projectile : battle.projectiles) if(!projectile.active && reserved < stationary_callback_count) {
            projectile = {};
            projectile.active = 1;
            projectile.magic = 9;
            projectile.phase = 2; // reserved until the native 12+30 setup completes
            projectile.target_side = int8_t(1-side);
            projectile.source_script = int16_t(skill->script);
            projectile.kind = uint8_t(reserved); // 0=3920, 1=3921; one object per group.
            projectile.owner_index = uint8_t(reserved);
            ++reserved;
        }
    }
    if(yellow_dragon_flame_count) {
        int spawn_tick=215; // MAGIC01 3600's 20+195 loop before mode2 starts 3603.
        // MAGIC01 3603 draws height, then a 4..10 wait for its first object;
        // each remaining object draws x, y, height, then another wait. Use a
        // private copy of the recovered native RNG so the visual approximation
        // does not perturb battle combat RNG. The concurrent 3600/3602 child
        // draws are not replayed here, so the exact native random stream is
        // still approximate.
        uint32_t visual_random=campaign.random;
        uint32_t visual_random_secondary=campaign.random_secondary;
        if(!visual_random && !visual_random_secondary) {
            visual_random=original::random_initial_primary;
            visual_random_secondary=original::random_initial_secondary;
        }
        auto visual_roll=[&](int limit) {
            const uint32_t next_primary=visual_random+1;
            const uint32_t previous_secondary=visual_random_secondary-1;
            const unsigned secondary_shift=next_primary&31;
            visual_random_secondary=(previous_secondary>>secondary_shift)+
                (previous_secondary<<((32-secondary_shift)&31));
            const unsigned primary_shift=visual_random_secondary&31;
            visual_random=(next_primary<<primary_shift)+
                (next_primary>>((32-primary_shift)&31));
            return limit>0 ? int((visual_random+visual_random_secondary)%uint32_t(limit)) : 0;
        };
        for(int i=0;i<yellow_dragon_flame_count;++i) {
            Projectile* flame=nullptr;
            for(auto& candidate : battle.projectiles) if(!candidate.active) {
                flame=&candidate;
                break;
            }
            if(!flame) break;
            const int native_x=i ? visual_roll(641)-320 : 0;
            const int native_y=i ? visual_roll(401)-100 : 0;
            const int native_z=320+visual_roll(41);
            *flame={};
            flame->active=1;
            flame->magic=23;
            flame->phase=0;
            flame->remaining=uint8_t(spawn_tick-215);
            flame->source_script=2617;
            flame->dx=int16_t(battle.tick); // Independent child cast clock.
            flame->target_side=int8_t(side);
            flame->scatter=uint8_t(battle.magic_field_center_x);
            flame->target_offset_x=215;
            flame->x=int16_t(f.x+native_x*battle_grid_column_width/96);
            flame->y=int16_t(battle_commander_y+native_y*battle_grid_row_height/72);
            flame->height=int16_t(native_z*battle_grid_row_height/72);
            // 3603 waits after every creation, including its last one before
            // setting shared global38=2.
            spawn_tick+=4+visual_roll(7);
        }
        yellow_dragon_flame_waits=spawn_tick-215;
    }
    if(yellow_dragon_fire_count) {
        // Mode2 launches child3602 after child3601's random10..50 wait,
        // sixty movement updates and a thirty-update pause. demake creates
        // ten 3608/3609/3610 fire objects, each with a 60-tick
        // sequence and native23 vertical speed10..30. The visual children
        // share the original RNG asynchronously; this private replay preserves
        // the source ranges and local call order without perturbing battle RNG,
        // but cannot reproduce their interleaving exactly.
        uint32_t visual_random=campaign.random ^ (uint32_t(battle.tick)*0x9e3779b9u) ^
            (uint32_t(side+1)*0x36023602u);
        uint32_t visual_random_secondary=campaign.random_secondary ^ 0x36023602u;
        if(!visual_random && !visual_random_secondary) {
            visual_random=original::random_initial_primary;
            visual_random_secondary=original::random_initial_secondary;
        }
        auto visual_roll=[&](int limit) {
            const uint32_t next_primary=visual_random+1;
            const uint32_t previous_secondary=visual_random_secondary-1;
            const unsigned secondary_shift=next_primary&31;
            visual_random_secondary=(previous_secondary>>secondary_shift)+
                (previous_secondary<<((32-secondary_shift)&31));
            const unsigned primary_shift=visual_random_secondary&31;
            visual_random=(next_primary<<primary_shift)+
                (next_primary>>((32-primary_shift)&31));
            return limit>0 ? int((visual_random+visual_random_secondary)%uint32_t(limit)) : 0;
        };
        int spawn_tick=120+visual_roll(41); // WAIT10..50, 60 steps, WAIT30.
        bool general_targeted=false;
        for(int i=0;i<yellow_dragon_fire_count;++i) {
            Projectile* fire=nullptr;
            for(auto& candidate : battle.projectiles) if(!candidate.active) {
                fire=&candidate;
                break;
            }
            if(!fire) break;
            const int native_x=visual_roll(97)-48;
            const int native_y=visual_roll(33)-16;
            const int variant=visual_roll(3); // sequence3608, 3609, or 3610.
            const int native_speed=10+visual_roll(21);
            *fire={};
            fire->active=1;
            fire->magic=25;
            fire->kind=uint8_t(variant);
            fire->dx=int16_t(native_speed);
            fire->target_side=int8_t(1-side);
            fire->source_script=2617;
            fire->x=int16_t(clamp(int(f.x)+native_x*battle_grid_column_width/96,
                                  0,battlefield_width-1));
            fire->y=int16_t(clamp(battle_commander_y+native_y*battle_grid_row_height/72,
                                  0,battle_grid_max_y));
            fire->height=int16_t(20*battle_grid_row_height/72);
            fire->target_offset_x=int16_t(spawn_tick);
            fire->collision_enabled=1; // Per-cast general-hit latch, not global child state.
            // demake's FireDown objects choose a live enemy soldier first;
            // with no soldiers left, one object has a 20% chance to target
            // the opposing general and the rest scatter around the army.
            int live_indices[200], live_count=0;
            for(int soldier_index=0;soldier_index<200;++soldier_index)
                if(battle.soldiers[(1-side)*200+soldier_index].hp>0)
                    live_indices[live_count++]=soldier_index;
            if(live_count) {
                const auto& target=battle.soldiers[(1-side)*200+live_indices[roll(live_count)]];
                fire->x=target.x;
                fire->y=target.y;
            } else if(!general_targeted && roll(100)<20) {
                general_targeted=true;
                fire->x=battle.sides[1-side].x;
                fire->y=battle_commander_y;
            } else {
                fire->x=int16_t(clamp(int(battle.sides[1-side].x)+visual_roll(301)-150,
                                      0,battlefield_width-1));
                fire->y=int16_t(clamp(battle_commander_y+visual_roll(161)-80,
                                      0,battle_grid_max_y));
            }
            spawn_tick+=visual_roll(6); // MAGIC01 3602 WAIT RNG(0,5).
        }
        // Child3602 sets shared var38 only after its final random wait and
        // WAIT15. Root3600 has already finished its 195-tick loop at age215,
        // but waits for that child before starting MAGIC01 3603. The earlier
        // model always began the nine 3603 flame objects at age215, even when
        // child3602's own random waits kept the source barrier closed.
        yellow_dragon_flame_start=maximum(215,spawn_tick+15);
        const int barrier_delay=yellow_dragon_flame_start-215;
        if(barrier_delay) for(auto& flame : battle.projectiles)
            if(flame.active && flame.magic==23 && flame.source_script==2617)
                flame.remaining=uint8_t(int(flame.remaining)+barrier_delay);
    }
    if(yellow_dragon_flame_count) {
        // 3600 waits for var38==2, then WAIT15 before native272. The 3603
        // child reaches var38==2 after its nine 4..10 waits; both its sampled
        // barrier start and those waits now determine this cast's busy end.
        const int busy_end=yellow_dragon_flame_start+yellow_dragon_flame_waits+15;
        battle.effect_remaining=uint16_t(busy_end);
        // 2617 does not use the field-center y elsewhere; retain its sampled
        // cast duration here to avoid growing the tightly budgeted Battle.
        battle.magic_field_center_y=int16_t(busy_end);
    }
    if(arrow_count) {
        const bool fire_mode=skill->script>=2603 && skill->script<=2605;
        const int flight_direction=side==0 ? -1 : 1;
        int visual_delays[40]{};
        for(int shot=0;shot<arrow_count;++shot) visual_delays[shot]=1+roll(30);
        auto alloc_arrow_visual=[&](int magic,int x,int y,int delay,int kind,
                                    int native_height=0) -> Projectile* {
            for(auto& visual : battle.projectiles) if(!visual.active) {
                visual={};
                visual.active=1;
                visual.magic=uint8_t(magic);
                visual.kind=uint8_t(kind);
                visual.phase=0;
                visual.remaining=uint8_t(delay);
                visual.source_script=int16_t(skill->script);
                visual.target_side=int8_t(1-side);
                visual.dx=int16_t(flight_direction);
                visual.x=int16_t(x);
                visual.y=int16_t(y);
                visual.height=int16_t(native_height*battle_grid_row_height/72);
                return &visual;
            }
            return nullptr;
        };
        for(int shot=0;shot<arrow_count;++shot) {
            const int row=9-shot%10;
            const int column=shot/10;
            const int native_x=side==0 ? 5792+column*96 : -432-column*96;
            int x=native_x*battle_grid_column_width/96;
            // The original field wraps objects that begin outside its camera
            // rectangle back across the opposite edge of the battlefield.
            while(x<0 || x>=battlefield_width) {
                if(x<0) x=-x;
                if(x>=battlefield_width) x=2*battlefield_width-x-1;
            }
            const int y=(180+row*72)*battle_grid_row_height/72;
            alloc_arrow_visual(18,x,y,50,side);
            if(side==0) {
                alloc_arrow_visual(26,x,y,50+visual_delays[shot],0);
            } else {
                // MAGIC01 3001 positions sequence3002 at native (+64,+104)
                // from its parent, then gives it azimuth128/elevation32.
                alloc_arrow_visual(31,
                    clamp(x+64*battle_grid_column_width/96,0,battlefield_width-1),
                    clamp(y+104*battle_grid_row_height/72,0,battle_grid_max_y),
                    50+visual_delays[shot],fire_mode ? 1 : 0);
            }
            if(fire_mode) {
                Projectile* fire_visual=alloc_arrow_visual(19,
                clamp(x+(side==0 ? -27 : 27)*battle_grid_column_width/96,
                      0,battlefield_width-1),y,50,0,14);
                if(fire_visual) fire_visual->target_offset_x=int16_t(visual_delays[shot]);
            }
        }
    }
    if(arrow_count && enemy_soldier_count > 0) {
        const int wave_count = minimum(arrow_count,enemy_soldier_count);
        const int flight_direction = side == 0 ? -1 : 1;
        const int target_side = 1-side;
        // The direct/scatter tail of MAGIC01 3100 uses native297 once to
        // choose a formation anchor (or the opposing general if none).
        const int scatter_anchor_x = native297_anchor_x(battle,target_side);
        for(int shot = 0; shot < arrow_count; ++shot) {
            Projectile* arrow = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                arrow = &projectile;
                break;
            }
            if(!arrow) break;
            *arrow = {};
            // MAGIC01 3100 samples native269 only for the target-relative
            // portion; the remaining direct arrows use scatter coordinates.
            const bool targeted=shot<wave_count;
            const int target_index=targeted ?
                enemy_soldiers[roll(enemy_soldier_count)] : 0xff;
            arrow->active = 1;
            arrow->magic = 2;
            arrow->kind = 1; // Defer its child wait until MAGIC01 3100 selects targets.
            arrow->height=int16_t(original::magic_effect_launch_delay(skill->script));
            arrow->phase = 0;
            arrow->scatter = uint8_t(shot >= wave_count);
            arrow->target_side = int8_t(target_side);
            arrow->owner_index = uint8_t(target_index);
            arrow->source_script = int16_t(skill->script);
            if(arrow->scatter) {
                const int native_scatter = roll(641) - 320;
                arrow->target_offset_x = int16_t(native_scatter * battle_grid_column_width / 96);
                // Direct 3003/3004 children scatter around the native297
                // anchor, then native40 offsets the object 260 native pixels
                // opposite its flight direction before it starts moving.
                arrow->x = int16_t(clamp(scatter_anchor_x+
                    native_scatter*battle_grid_column_width/96-
                    flight_direction*260*battle_grid_column_width/96,
                    0,battlefield_width-1));
                arrow->y = int16_t((150 + roll(501)) * battle_grid_row_height / 72);
                // Parent3100 draws x/y scatter before passing the child's
                // 80+RNG(1,30) delay.
                arrow->remaining = uint8_t(81 + roll(30));
            } else {
                // Target-relative child delay follows native269 selection.
                arrow->remaining = uint8_t(81 + roll(30));
            }
            arrow->dx = int16_t(flight_direction);
        }
    } else if(arrow_count) {
        // MAGIC01 3100 still enters its side-0 (6203) child branch when
        // native269 returns handle 0: native261/262 leave the native result
        // at zero, and the bytecode does not guard the 6203/3003 spawns on
        // that handle. The first child starts relative to the caster; the
        // remaining direct 3003 children scatter around native297's
        // formation anchor, which falls back to the opposing general.
        const int targetless_direction = side == 0 ? -1 : 1;
        const int scatter_anchor_x = native297_anchor_x(battle,1-side);
        for(int shot = 0; shot < arrow_count; ++shot) {
            Projectile* arrow = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                arrow = &projectile;
                break;
            }
            if(!arrow) break;
            *arrow = {};
            arrow->active = 1;
            arrow->magic = 2;
            arrow->kind = 1; // Defer its child wait until MAGIC01 3100 selects targets.
            arrow->height=int16_t(original::magic_effect_launch_delay(skill->script));
            arrow->phase = 0;
            arrow->scatter = uint8_t(shot != 0);
            arrow->target_side = int8_t(1-side);
            arrow->owner_index = 0xff;
            arrow->source_script = int16_t(skill->script);
            arrow->dx = int16_t(targetless_direction);
            if(arrow->scatter) {
                arrow->target_offset_x = int16_t((roll(641)-320)*battle_grid_column_width/96);
                arrow->x = int16_t(clamp(scatter_anchor_x+arrow->target_offset_x+
                    -260*targetless_direction*battle_grid_column_width/96,
                    0,battlefield_width-1));
                arrow->y = int16_t((150 + roll(501))*battle_grid_row_height/72);
                arrow->remaining = uint8_t(81 + roll(30));
            } else {
                arrow->remaining = uint8_t(81 + roll(30));
            }
        }
    }
    if(ghost_projectile_count) {
        const int flight_direction = side == 0 ? -1 : 1;
        for(int shot = 0; shot < ghost_projectile_count; ++shot) {
            Projectile* arrow = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                arrow = &projectile;
                break;
            }
            if(!arrow) break;
            *arrow = {};
            arrow->active = 1;
            arrow->magic = 2;
            arrow->phase = 0;
            arrow->target_side = int8_t(1-side);
            arrow->source_script = int16_t(skill->script);
            arrow->dx = int16_t(flight_direction);
            arrow->owner_index = 0xff;
            // GLOBAL 6200 waits20+30 for setup, then waits another60 before
            // target selection and the 80+RNG(1,30) child launch delays.
            arrow->scatter = 2;
            arrow->remaining = uint8_t(original::magic_busy_duration(skill->script));
        }
    }
    if(mine_count) {
        // MAGIC01 3500 waits 20 + 34 scheduler ticks before launching GLOBAL
        // 2503 children. Each child then waits RNG(1, 30), creates one 3500
        // mine around the caster, and gives it the 3104 collision callback.
        for(int mine = 0; mine < mine_count; ++mine) {
            Projectile* object = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                object = &projectile;
                break;
            }
            if(!object) break;
            *object = {};
            object->active = 1;
            object->magic = 3;
            object->phase = 0;
            object->remaining = 54;
            object->target_side = int8_t(1-side);
            object->owner_index = 0xff;
            object->source_script = int16_t(skill->script);
        }
    }
    if(sword_wave_count) {
        // MAGIC01 4400 waits 20+15 updates, then launches one child per
        // update. Its native21 position uses angle128 and a radius derived
        // from local1*16; native24 raises each sequence4400 from z64 by two.
        const int step = sword_wave_count == 4 ? 64 : sword_wave_count == 8 ? 32 : 16;
        for(int shot = 0; shot < sword_wave_count; ++shot) {
            Projectile* object = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                object = &projectile;
                break;
            }
            if(!object) break;
            *object = {};
            const int native_radius = 240 - shot * step;
            object->active = 1;
            object->magic = 4;
            object->phase = 0;
            object->remaining = uint8_t(35 + shot);
            object->x = int16_t(f.x - native_radius * battle_grid_column_width / 96);
            object->y = battle_commander_y;
            object->height = 64;
            object->target_side = int8_t(1-side);
            object->owner_index = uint8_t(shot);
            // MAGIC01 4402 passes global36 as native17's orientation (128
            // for side0, 0 for side1). Store facing separately; the child
            // starts immediately after its real script barrier.
            object->dx = int16_t(side == 0 ? -1 : 1);
            object->scatter = 0;
            object->source_script = int16_t(skill->script);
        }
    }
    if(heaven_beam_count || heaven_strike_count) {
        static constexpr int16_t native_y[8] = {36,108,180,252,756,828,900,972};
        const int direction = side == 0 ? 1 : -1;
        const int beam_x_offset = 700*battle_grid_column_width/96;
        const int beam_anchor_x = native297_anchor_x(battle,1-side);
        for(int i=0;i<heaven_beam_count;++i) {
            Projectile* beam = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                beam=&projectile;
                break;
            }
            if(!beam) break;
            *beam={};
            beam->active=1;
            beam->magic=7;
            beam->phase=0;
            beam->remaining=uint8_t(original::heaven_and_earth_beam_launch_delay(skill->script)+roll(16));
            beam->height=int16_t(original::heaven_and_earth_beam_lifetime(skill->script));
            beam->x=int16_t(clamp(beam_anchor_x+direction*beam_x_offset,0,battlefield_width-1));
            beam->y=int16_t(int(native_y[i])*battle_grid_row_height/72);
            beam->dx=int16_t(direction);
            beam->target_side=int8_t(1-side);
            beam->owner_index=0xff;
            beam->kind=uint8_t((i&1)==1); // sequence 4000 is the visible M010 object.
            beam->source_script=int16_t(skill->script);
        }
        for(int i=0;i<heaven_strike_count;++i) {
            Projectile* thunder = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                thunder=&projectile;
                break;
            }
            if(!thunder) break;
            *thunder={};
            thunder->active=1;
            thunder->magic=8;
            thunder->phase=0;
            thunder->remaining=uint8_t(original::heaven_and_earth_general_launch_delay(skill->script));
            thunder->height=int16_t(original::heaven_and_earth_general_checks(skill->script));
            thunder->x=battle.sides[1-side].x;
            thunder->y=battle_commander_y;
            thunder->target_side=int8_t(1-side);
            thunder->owner_index=uint8_t(i);
            thunder->source_script=int16_t(skill->script);
        }
    }
    if(chariot_object_count) {
        for(int lane=0;lane<chariot_count;++lane) for(int kind=0;kind<2;++kind) {
            Projectile* visual=nullptr;
            for(auto& candidate : battle.projectiles) if(!candidate.active) {
                visual=&candidate;
                break;
            }
            if(!visual) break;
            *visual={};
            visual->active=1;
            visual->magic=20;
            visual->phase=2; // GLOBAL4000 starts both children after update65.
            visual->source_script=int16_t(skill->script);
            visual->target_side=int8_t(side);
            visual->owner_index=uint8_t(lane);
            visual->kind=uint8_t(kind); // 0=NULSHAPE proxy, 1=visible M010 cart.
        }
    }
    // Composite children overlap. A later child replaces the global cast
    // timer before a previous rock/cart windup ends; retain that launch in
    // an independent, invisible scheduler token instead of stranding proxies.
    if(battle.magic_composite_bootstrap && (chariot_count || rockfall_count)) {
        for(auto& token:battle.projectiles) if(!token.active) {
            token={}; token.active=1; token.magic=34;
            token.source_script=int16_t(skill->script);
            token.target_side=int8_t(side);
            token.remaining=uint8_t(original::magic_effect_launch_delay(skill->script));
            token.owner_index=uint8_t(battle.tick);
            break;
        }
    }
    if(reinforcement_count>0)
        for(auto& marker:battle.projectiles) if(!marker.active) {
            marker={};marker.active=1;marker.magic=36;
            marker.source_script=int16_t(skill->script);marker.target_side=int8_t(side);
            marker.remaining=15;
            break;
        }
    notice = original::reinforcement_amount(skill->script) && !reinforcement_count ? "兵力已满" : skill->name;
    return true;
}

bool Game::start_magic_composite_child() {
    if(!battle.magic_composite_kind || battle.magic_composite_side>1) return false;
    static constexpr int heaven_children[3]={2617,2628,2626};
    static constexpr int ghost_children[3]={2604,2622,2646};
    const int script=battle.magic_composite_kind==1 ?
        heaven_children[battle.magic_composite_stage] :
        ghost_children[battle.magic_composite_stage];
    const SkillDef* child=skill_by_script(script);
    if(!child) {
        battle.magic_composite_kind=0;
        battle.effect_remaining=0;
        return false;
    }
    const int side=battle.magic_composite_side;
    // Reservations cannot be stolen by ordinary arrows between child starts.
    int released=0,free_before=0;
    for(auto& p:battle.projectiles) {
        if(p.active && p.magic==37 && p.target_side==side) {p={};++released;}
        free_before+=!p.active;
    }
    const int mp=battle.sides[side].mp;
    const uint32_t charge=battle.sides[side].charge;
    // Child scripts reuse their normal visual/collision setup but the demake
    // composite pays only its own MP and consumes the sword meter once.
    battle.magic_composite_bootstrap=1;
    battle.sides[side].mp=maximum(0,int(battle.sides[side].mp)+child->mp);
    battle.sides[side].charge=original::cast_charge_max;
    const bool started=cast_battle_skill(side,child);
    if(started && released) {
        int free_after=0;for(const auto& p:battle.projectiles) free_after+=!p.active;
        int reserve=maximum(0,released-(free_before-free_after));
        for(auto& p:battle.projectiles) if(!p.active && reserve) {
            p={};p.active=1;p.magic=37;p.target_side=int8_t(side);--reserve;
        }
    }
    battle.magic_composite_bootstrap=0;
    battle.sides[side].mp=int16_t(mp);
    campaign.officers[battle.sides[side].general].mp=uint16_t(mp);
    battle.sides[side].charge=charge;
    if(!started) {
        battle.magic_composite_kind=0;
        battle.magic_composite_stage=0;
        battle.effect_remaining=0;
        return false;
    }
    return true;
}

[[gnu::noinline]] void Game::update_cyclone_projectile(Projectile& p) {
    if(p.phase==0) {
        if(p.remaining && --p.remaining) return;
        p.phase=2;
        p.height=0;
        p.remaining=uint8_t(54+18*(int(p.scatter)-1-int(p.owner_index)));
        if(p.collision_enabled && p.target_side>=0 && p.target_side<2) {
            auto& force=battle.sides[int(p.target_side)];
            // demake ShowTornado's one 100x100 enemy-only hit per activation.
            for(int i=0;i<200;++i) {
                auto& soldier=battle.soldiers[int(p.target_side)*200+i];
                if(soldier.hp<=0 || soldier.x<p.x-50 || soldier.x>p.x+50 ||
                   soldier.y<p.y-50 || soldier.y>p.y+50) continue;
                soldier.hp=0;--force.soldiers;
                if(!soldier.rear) --force.front;
            }
            // demake MagicConfig.xml ATTACK=8, applied once if the general is
            // inside this cyclone's rectangle (not every animation frame).
            if(force.hp>0 && force.x>=p.x-50 && force.x<=p.x+50 &&
               battle_commander_y>=p.y-50 && battle_commander_y<=p.y+50)
                apply_magic_general_callback(p.source_script,p.target_side,0);
            p.collision_enabled=0;
        }
        return;
    }
    ++p.height;
    if(p.remaining && !--p.remaining) p.active=0;
}

// Launch setup stays in ROM; the travelling projectile loop needs fast RAM.
void Game::update_magic_projectile_windup(Projectile& p) {
    if(p.remaining && --p.remaining) return;
    p.phase=1;
    if(p.source_script!=2633 && p.source_script!=2660) return;
    const auto& caster=battle.sides[1-p.target_side];
    const auto& target=battle.sides[p.target_side];
    // Reacquire at release, not at the start of the casting animation: either
    // general can cross the other during the wind-up. Side is not direction.
    p.x=caster.x;
    p.y=battle_commander_y;
    const int distance=int(target.x)-int(caster.x);
    p.dx=int16_t(distance>0 ? 3 : distance<0 ? -3 : p.dx);
    if(target.hp>0 && distance>=-20 && distance<=20) {
        // At contact the first movement step can already leave the target's
        // rounded cell. Resolve the arrow at the muzzle instead of skipping it.
        apply_magic_general_callback(p.source_script,p.target_side,0);
        p.collision_enabled=0;
        p.remaining=1;
    }
}

void Game::apply_magic_general_callback(int script,int target_side,uint8_t mode) {
    if(target_side < 0 || target_side > 1) return;
    int base = mode == 0 ? original::magic_general_hit_bonus(script) : battle.magic_callback_damage;
    if(mode == 2) {
        base += base*level(battle.sides[1-target_side].general)/100;
        // MAGIC01 3103 applies this caster-scaled hit, then starts GLOBAL2520
        // with the same global44 value for a second, unscaled hit.
        base += battle.magic_callback_damage;
    }
    else if(mode == 3) base = original::magic_general_hit_damage(
        script,level(battle.sides[1-target_side].general));
    auto& target = battle.sides[target_side];
    if(!battle.immunity_ticks[target_side])
        target.hp = int16_t(maximum(0,int(target.hp)-base));
    if(mode == 1 || mode == 2) {
        const int next_base = original::magic_general_callback_next_base(script);
        if(next_base) battle.magic_callback_damage = uint8_t(next_base);
        if(script==2665 && !battle.magic_field_iterations)
            battle.magic_callback_damage=0;
    }
    if((script>=2633 && script<=2635) || script==2660) {
        // GLOBAL 4202 creates sequence4210/4211 unless global40==2
        // (skill2635), with two native40 jitter draws followed by eight
        // 1+RNG(5) waits on its attached effect object.
        const bool show_burst=script!=2635;
        int jitter_x=0, jitter_y=0;
        if(show_burst) {
            jitter_x=roll(96)-48;
            jitter_y=roll(96)-48;
        }
        int lifetime=0;
        for(int wait=0;wait<8;++wait) lifetime+=1+roll(5);
        if(show_burst) {
            Projectile* burst=nullptr;
            for(auto& candidate : battle.projectiles)
                if(!candidate.active) { burst=&candidate; break; }
            if(burst) {
                *burst={};
                burst->active=1;
                burst->magic=22;
                burst->phase=1;
                burst->kind=uint8_t(script==2660 ? 0 : 1);
                burst->source_script=int16_t(script);
                burst->target_side=int8_t(target_side);
                burst->x=int16_t(clamp(int(target.x)+
                    jitter_x*battle_grid_column_width/96,0,battlefield_width-1));
                burst->y=int16_t(battle_commander_y+
                    32*battle_grid_row_height/72+
                    jitter_y*battle_grid_row_height/72);
                // Sequence4210/4211 contains seven four-update image slots;
                // the final slot repeats B6/Y6, so its natural cap is28.
                burst->remaining=uint8_t(minimum(28,lifetime));
            }
        }
    }
}

void Game::spawn_m014_sparks(int x,int y) {
    M014SparkEmitter* emitter=nullptr;
    for(auto& candidate : battle.m014_spark_emitters) {
        if(candidate.active && uint16_t(battle.tick-candidate.start_tick)>=64)
            candidate.active=0;
        if(!candidate.active && !emitter) emitter=&candidate;
    }
    if(emitter) {
        emitter->x=int16_t(x);
        emitter->y=int16_t(y);
        emitter->start_tick=battle.tick;
        emitter->active=1;
    }
    // Preserve native17/native24's exact per-particle visual selections in
    // EWRAM while keeping the renderer free of per-frame RNG reconstruction.
    for(int i=0;i<20;++i) {
        const uint8_t variant=uint8_t(roll(2)); // native17 selects sequence4425/4426.
        (void)roll(256); // native17 consumes a random facing angle.
        const uint8_t azimuth=uint8_t(roll(257));
        const uint8_t elevation=uint8_t(16+roll(97));
        if(emitter) {
            emitter->variants[i]=variant;
            emitter->azimuths[i]=azimuth;
            emitter->elevations[i]=elevation;
        }
    }
}

void Game::update_magic_field() {
    if((battle.effect_script==2666 || battle.effect_script==2667) &&
       battle.heal_visual_remaining) {
        // GLOBAL6300 starts its 60 one-update particle loop at cast age35.
        // Each particle draws native24 azimuth RNG(0,255) and native29
        // gravity RNG(256,2560). Speed0 makes the visual stationary, but the
        // source still advances the shared battle random stream.
        const int heal_age=126-int(battle.heal_visual_remaining);
        if(heal_age>=35 && heal_age<95)
            for(int particle=0;particle<3;++particle) {
                (void)roll(256);
                (void)roll(2305);
            }
    }
    if(battle.effect_script>=2639 && battle.effect_script<=2641 &&
       battle.magic_field_state==3) {
        if(battle.magic_field_wait && --battle.magic_field_wait) return;
        const int groups=1+roll(2); // MAGIC01 4411 samples RNG(1,2).
        for(int i=0;i<groups;++i) {
            const int x=int(battle.magic_field_center_x)+
                (roll(97)-48)*battle_grid_column_width/96;
            const int y=int(battle.magic_field_center_y)+
                (roll(33)-16)*battle_grid_row_height/72;
            if(roll(2)==0) {
                spawn_m014_sparks(x,y);
            } else {
                Projectile* child=nullptr;
                for(auto& candidate : battle.projectiles) if(!candidate.active) {
                    child=&candidate;
                    break;
                }
                if(child) {
                    *child={};
                    child->active=1;
                    child->magic=35; // MAGIC01 4412 waits eight updates.
                    child->source_script=battle.effect_script;
                    child->x=int16_t(x);
                    child->y=int16_t(y);
                    child->remaining=8;
                }
            }
        }
        if(battle.magic_field_iterations) --battle.magic_field_iterations;
        if(battle.magic_field_iterations) battle.magic_field_wait=1;
        else battle.magic_field_state=0;
        return;
    }
    if(!battle.magic_field_iterations || !battle.magic_field_wait) return;
    if(--battle.magic_field_wait) return;
    if(battle.effect_script>=2639 && battle.effect_script<=2641) {
        if(battle.magic_field_state==2) {
            // GLOBAL4420 selects one live enemy soldier with native269 (or
            // the enemy general when the force has no soldiers), then waits
            // for the first of three 4410 children, eight updates, and its
            // thirteen one-update positioning steps before launching 4411.
            int live_count=0;
            for(int i=0;i<200;++i)
                if(battle.soldiers[(1-battle.effect_side)*200+i].hp>0) ++live_count;
            if(live_count) {
                int rank=roll(live_count), current=0;
                for(int i=0;i<200;++i) {
                    const auto& target=battle.soldiers[(1-battle.effect_side)*200+i];
                    if(target.hp<=0) continue;
                    if(current++==rank) {
                        battle.magic_field_center_x=target.x;
                        battle.magic_field_center_y=target.y;
                        break;
                    }
                }
            } else {
                battle.magic_field_center_x=battle.sides[1-battle.effect_side].x;
                battle.magic_field_center_y=battle_commander_y;
            }
            // Three 4410 children each perform two RNG(1,16) waits.  The
            // parent barrier opens when the first child finishes.
            const int first_child=roll(16)+1+roll(16)+1;
            const int second_child=roll(16)+1+roll(16)+1;
            const int third_child=roll(16)+1+roll(16)+1;
            const int barrier=minimum(first_child,minimum(second_child,third_child));
            battle.magic_field_state=1;
            battle.magic_field_wait=uint8_t(barrier+21); // barrier + wait8 + 13 setup ticks
            return;
        }
        static constexpr int8_t offset_x[13]={-40,0,40,-40,0,40,-40,0,40,80,-80,0,0};
        static constexpr int8_t offset_y[13]={-30,-30,-30,0,0,0,30,30,30,0,0,60,-60};
        const int object=13-int(battle.magic_field_iterations);
        Projectile* field=nullptr;
        for(auto& candidate : battle.projectiles) if(candidate.active &&
            candidate.magic==10 && candidate.phase==2 &&
            candidate.source_script==battle.effect_script) {
            field=&candidate;
            break;
        }
        if(field && object>=0 && object<13) {
            *field={};
            field->active=1;
            field->magic=10;
            field->phase=1;
            field->remaining=8;
            field->target_side=int8_t(1-battle.effect_side);
            field->source_script=battle.effect_script;
            field->x=int16_t(battle.magic_field_center_x+offset_x[object]);
            field->y=int16_t(battle.magic_field_center_y+offset_y[object]);
        }
        --battle.magic_field_iterations;
        if(battle.magic_field_iterations) battle.magic_field_wait=1;
        return;
    }
    if(battle.effect_script==2665) {
        // M022 remains the GBA visual carrier, but demake's script has only
        // four commander strikes (or one non-damaging pass at long range),
        // rather than the native PC's 19/38-cell collision waves.
        const int total=battle.magic_field_state ? 4 : 1;
        const int pass=total-int(battle.magic_field_iterations);
        auto activate_visual=[&](int slot,int kind,int x,int y,int native_height) {
            for(auto& visual : battle.projectiles) if(visual.active && visual.magic==13 &&
                visual.phase==2 && visual.source_script==2665 && visual.owner_index==slot) {
                visual.phase=0;
                visual.kind=uint8_t(kind);
                visual.target_side=int8_t(1-battle.effect_side);
                visual.x=int16_t(clamp(x,0,battlefield_width-1));
                visual.y=int16_t(clamp(y,0,battle_grid_max_y));
                // Store native z units here: the renderer performs the one
                // native-to-battlefield projection for these M022 objects.
                visual.height=int16_t(native_height);
                // GLOBAL6260 passes global36 (128/0) to native17 as facing.
                // The actual object lifetime is the sequence's 16-update
                // native38 timer, so don't turn that angle into a wait.
                visual.dx=int16_t(battle.effect_side==0 ? -1 : 1);
                visual.remaining=0;
                break;
            }
        };
        const int sequence=pass%3+1;
        const int native_height=48+roll(33);
        const int native_x_offset=roll(65)-32;
        activate_visual(pass,sequence,
            int(battle.magic_field_center_x)+native_x_offset*battle_grid_column_width/96,
            battle_commander_y,native_height);

        Projectile* field = nullptr;
        for(auto& candidate : battle.projectiles) if(candidate.active &&
            candidate.magic==10 && candidate.phase==2 &&
            candidate.source_script==battle.effect_script) {
            field=&candidate;
            break;
        }
        if(field && battle.magic_field_state) {
            *field={};
            field->active=1;
            field->magic=10;
            field->phase=1;
            field->remaining=1;
            field->collision_enabled=1;
            field->target_side=int8_t(1-battle.effect_side);
            field->source_script=battle.effect_script;
            field->x=battle.magic_field_center_x;
            field->y=battle.magic_field_center_y;
        } else if(field) {
            // Long-range demake path still shows its first visual, but has no
            // collision callback at all.
            field->active=0;
        }
        --battle.magic_field_iterations;
        if(battle.magic_field_iterations) {
            battle.magic_field_wait=5;
        } else {
            battle.magic_field_wait=0;
            battle.magic_field_state=0;
        }
        return;
    }
    static constexpr int16_t offset_x[13] = {
        -96,0,96, -96,0,96, -96,0,96, 192,-192,0,0
    };
    static constexpr int16_t offset_y[13] = {
        -72,-72,-72, 0,0,0, 72,72,72, 0,0,144,-144
    };
    for(int object=0; object<13; ++object) {
        Projectile* field = nullptr;
        for(auto& candidate : battle.projectiles) if(candidate.active &&
            candidate.magic==10 && candidate.phase==2 &&
            candidate.source_script==battle.effect_script) {
            field=&candidate;
            break;
        }
        if(!field) break; // cast_skill reserves the full burst pool
        *field={};
        field->active=1;
        field->magic=10;
        field->phase=1;
        field->remaining=8;
        field->target_side=int8_t(1-battle.effect_side);
        field->source_script=battle.effect_script;
        field->x=int16_t(battle.magic_field_center_x+
            offset_x[object]*battle_grid_column_width/96);
        field->y=int16_t(battle.magic_field_center_y+
            offset_y[object]*battle_grid_row_height/72);
    }
    if(battle.effect_script>=2657 && battle.effect_script<=2659) {
        const int pass=original::magic_field_iteration_count(battle.effect_script)-
            int(battle.magic_field_iterations);
        const int first=pass*22;
        for(auto& visual : battle.projectiles) if(visual.active && visual.magic==13 &&
            visual.phase==2 && visual.source_script==battle.effect_script &&
            visual.owner_index>=first && visual.owner_index<first+22) {
            const int slot=int(visual.owner_index)-first;
            visual.x=battle.magic_field_center_x;
            visual.y=battle.magic_field_center_y;
            visual.height=0;
            visual.remaining=0;
            visual.phase=1;
            if(slot<2) {
                visual.kind=uint8_t(slot); // pillar sequence and brief M020swc flare
            } else {
                visual.magic=14;
                visual.phase=0; // GLOBAL6001/6002 initialize each random fragment.
                // GLOBAL6000 sets var43 once per cast: variants2657/2658 use
                // GLOBAL6001 (speed6), while2659 uses GLOBAL6002 (speed7).
                // Both GLOBAL2511 children start that same particle script.
                visual.owner_index=uint8_t(battle.effect_script==2659 ? 7 : 6);
            }
        }
    }
    // The root waits RNG(1,8), then chooses a fresh center even after its
    // last pass. Preserve this draw order for later battle randomness.
    const uint8_t next_wait=uint8_t(1+roll(8));
    // The cast began with a conservative upper bound of eight updates for
    // every loop wait. Native GLOBAL6000 waits for the actual RNG(1,8) value
    // after each pass before its final 120-update tail and native272 unlock.
    // Shorten the remaining busy time by the unused part of this pass's bound;
    // do it at the original draw site so RNG ordering stays unchanged.
    if(battle.effect_script>=2657 && battle.effect_script<=2659 &&
       battle.effect_remaining && next_wait<8)
        battle.effect_remaining=uint16_t(battle.effect_remaining-(8-next_wait));
    const int random_center_x=(roll(481)-240)*battle_grid_column_width/96;
    const int random_center_y=(roll(451)-110)*battle_grid_row_height/72;
    if(battle.effect_script==2659) {
        // Five bursts: caster first, then four outward directions. The old
        // native-coordinate scatter was heavily biased down (+141/-45 world
        // y), so both visuals and collision centers drifted below the caster.
        // Keep the two RNG draws above, but use one shared center for art/hits.
        constexpr int next_x[5]={80,-80,0,0,0};
        constexpr int next_y[5]={0,0,-30,30,0};
        const int pass=5-int(battle.magic_field_iterations);
        battle.magic_field_center_x=int16_t(battle.magic_field_origin_x+next_x[pass]);
        battle.magic_field_center_y=int16_t(battle.magic_field_origin_y+next_y[pass]);
    } else {
        battle.magic_field_center_x=int16_t(battle.magic_field_origin_x+random_center_x);
        battle.magic_field_center_y=int16_t(battle.magic_field_origin_y+random_center_y);
    }
    --battle.magic_field_iterations;
    if(battle.magic_field_iterations) battle.magic_field_wait=next_wait;
    else if(battle.effect_script>=2639 && battle.effect_script<=2641) {
        // MAGIC01 4411 follows its thirteen collision placements with a
        // fifteen-pass, one-update loop; each pass starts one or two children.
        battle.magic_field_iterations=15;
        battle.magic_field_state=3;
        battle.magic_field_wait=1;
    }
}

void Game::apply_pending_effect() {
    if(battle.effect_applied || battle.pending_winner >= 0) return;
    int side = battle.effect_side;
    if(side < 0 || side > 1) return;
    auto& f = battle.sides[side];
    auto& enemy = battle.sides[1 - side];
    // Composite wrappers launch child scripts that are not present in the
    // caster's learned slots. Resolve the active script directly first, then
    // retain the learned-slot fallback for ordinary casts.
    const SkillDef* skill = skill_by_script(battle.effect_script);
    if(!skill) {
        skill = skill_by_id(learned_skill(f.general,0));
        for(int slot = 0; slot < skill_count(f.general); ++slot) {
            const auto* candidate = skill_by_id(learned_skill(f.general,slot));
            if(candidate && candidate->script == battle.effect_script) { skill = candidate; break; }
        }
    }
    if(!skill) return;
    if(original::stationary_callback_object_count(skill->script)) {
        // MAGIC01 3902 finishes its 12+30 update setup, then rolls the
        // 40..70 delay before starting two GLOBAL 2505 groups four updates apart.
        // The cast gate has already held the general through the setup.
        const int random_part = roll(31);
        const int launch_delay = 40 + random_part;
        for(auto& projectile : battle.projectiles) {
            if(!projectile.active || projectile.magic != 9 ||
               projectile.source_script != skill->script || projectile.phase != 2) continue;
            const int second_group_delay = projectile.kind ? 4 : 0;
            projectile.x = battle.sides[1-side].x;
            projectile.y = battle_commander_y;
            projectile.remaining = uint8_t(launch_delay + second_group_delay + 1);
            projectile.height = 0; // native38 starts a 16-update object timer after placement.
            projectile.scatter = 0; // GLOBAL2505 places the object six times.
            projectile.phase = 0;
        }
        // Tick 42 has just elapsed. Keep the cast locked for 104+RNG ticks,
        // yielding 146..176 total updates from the original 12+30 setup.
        battle.effect_remaining = uint16_t(105 + random_part);
        battle.effect_ticks = 0;
        battle.effect_applied = 1;
        return;
    }
    const int rockfall_count=original::rockfall_strike_count(skill->script);
    if(rockfall_count) {
        int live[200], live_count=0;
        for(int i=0;i<200;++i)
            if(battle.soldiers[(1-side)*200+i].hp>0) live[live_count++]=i;
        const int targeted=minimum(rockfall_count,live_count);
        int anchor_x=native297_anchor_x(battle,1-side);
        int used=0;
        for(auto& projectile : battle.projectiles) {
            if(!projectile.active || projectile.magic!=11 || projectile.phase!=2 ||
               projectile.source_script!=skill->script || used>=rockfall_count) continue;
            int target_x, target_y, owner=0xff;
            if(used<targeted) {
                // native269 samples a live enemy handle with replacement.
                owner=live[roll(live_count)];
                const auto& target=battle.soldiers[(1-side)*200+owner];
                target_x=target.x;
                target_y=target.y;
            } else {
                // GLOBAL3700's deficit path scatters around native297's
                // densest formation anchor instead of inventing guaranteed hits.
                target_x=clamp(anchor_x+(roll(641)-320)*battle_grid_column_width/96,0,battlefield_width-1);
                target_y=clamp((150+roll(501))*battle_grid_row_height/72,0,battle_grid_rows*battle_grid_row_height-1);
            }
            // MAGIC01 3705/3706 always creates the selected-target stone
            // at target+284 and native24(128,-32,32) flies left. The deficit
            // branch uses GLOBAL3703/3704 instead: side0 starts at x+260 and
            // flies left, while side1 starts at x-260 and flies right.
            const bool targeted_stone=owner<200;
            const int direction=targeted_stone || side==0 ? -1 : 1;
            const int launch_offset=targeted_stone ? 284 : 260;
            projectile.x=int16_t(clamp(target_x-direction*(launch_offset*battle_grid_column_width/96),
                                       0,battlefield_width-1));
            projectile.y=int16_t(target_y);
            // MAGIC01 3705/3706 pass angle 128, pitch -32 and speed 32 to
            // native24: both horizontal and vertical components are
            // 32*sin(pi/4) ~= 22.6 native units per update. Keep dx as the
            // direction flag; the tick applies the fractional grid velocity.
            projectile.dx=int16_t(direction);
            // Keep projectile elevation in GBA world units, matching the
            // native 72-unit row to 30-unit GBA row conversion used by the
            // renderer and the other pitched projectile families.
            projectile.height=int16_t(260*battle_grid_row_height/72);
            projectile.remaining=uint8_t(80+1+roll(30));
            projectile.phase=0;
            projectile.owner_index=uint8_t(owner);
            ++used;
        }
        battle.effect_applied=1;
        return;
    }
    if(skill->script==2627 || skill->script==2628) {
        // GLOBAL2508 creates one visible sequence4000 and one NULSHAPE
        // sequence2500 collision proxy on each lane. Native soldier-cell
        // contact dispatches immediate death; the GBA cart does not emit
        // generic callback3904 explosion particles on soldier collisions.
        const int chariot_count=original::chariot_visual_count(skill->script);
            int target_x=battle.magic_field_state ? battle.magic_field_center_x :
                                                         battle.sides[1-side].x;
            if(!battle.magic_field_state) {
                bool found=false;
                for(const auto& soldier : battle.soldiers) if(
                    soldier.hp>0 && soldier.side==1-side) {
                    if(!found || (side==0 ? soldier.x>target_x : soldier.x<target_x))
                        target_x=soldier.x;
                    found=true;
                }
                battle.magic_field_center_x=int16_t(target_x);
            }
            constexpr int native_y_mode0[2]={252,828};
            constexpr int native_y_mode1[4]={108,252,828,972};
            const int direction=side==0 ? 1 : -1;
            int reserved=0;
            for(auto& visual : battle.projectiles) if(visual.active &&
                visual.magic==20 && visual.phase==2 &&
                visual.source_script==skill->script && reserved<2*chariot_count) {
                const int lane=int(visual.owner_index);
                visual.x=int16_t(clamp(target_x+direction*(700*battle_grid_column_width/96),
                                       0,battlefield_width-1));
                const int lane_y=skill->script==2627 ? native_y_mode0[lane] :
                                                         native_y_mode1[lane];
                const int object_y=lane_y-(visual.kind==0 ? 72 : 0);
                visual.y=int16_t(object_y*battle_grid_row_height/72);
                visual.dx=int16_t(side==0 ? -8 : 8); // native24 speed8, azimuth128/0.
                visual.target_offset_x=0;
                visual.movement_remainder=0;
                visual.height=0;
                visual.remaining=uint8_t(roll(16)); // GLOBAL2508 WAIT(0..15).
                visual.phase=visual.remaining ? 0 : 1;
                visual.height=0;
                ++reserved;
            }
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_target(skill->script) == original::MagicTarget::visual_only) {
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_uses_scripted_field(skill->script)) {
        // GLOBAL6000/6260 collision objects resolve occupied-cell hits.
        // Their schedulers run in battle_tick; no cast-deadline batch hit.
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_uses_fire_wheel(skill->script)) {
        // Eight independently scheduled colliders were reserved at cast
        // start. Their angle controls placement/orientation; the actual
        // 20-update setup comes from MAGIC01 3300/3400. Never add batch damage.
        battle.effect_applied=1;
        return;
    }
    if(original::healing_amount(skill->script)) {
        // Native 6300 waits 20 + 15 scheduler ticks, heals one point for
        // 15 ticks, then applies the remainder in one capped update.
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_uses_heaven_and_earth(skill->script)) {
        // GLOBAL 6230 resolves through moving cell-collision beams and two
        // five-check fixed-damage thunder objects. EFFECT=0 must not use the
        // generic batch-loss/commander-damage fallback.
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_uses_ghost_crying_god(skill->script)) {
        // GLOBAL6200's confirmed 6205-6208 child projectiles resolve through
        // their own collision callbacks; do not also apply the generic batch.
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_uses_collision_projectile(skill->script)) {
        // These skills resolve from their moving object's grid collisions;
        // the 24-update cast visual is not a damage deadline.
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_uses_tornado(skill->script)) {
        // MAGIC01 4300 resolves from a jittering sequence-1012 collision
        // proxy. Soldier event 0x10003 removes one occupied soldier directly;
        // never apply the old fixed casualty batch at this deadline.
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_uses_arrow_volley(skill->script)) {
        // GLOBAL3100 selects live enemy soldiers through native269 and routes
        // damage through collidable arrow children. native269 returns a null
        // handle when no live soldier is available, so do not invent a generic
        // commander hit or force casualty at the cast deadline.
        battle.effect_applied = 1;
        return;
    }
    if(original::minefield_count(skill->script)) {
        // Each planted mine resolves its own occupied-cell collision.
        battle.effect_applied = 1;
        return;
    }
    if(original::sword_wave_projectile_count(skill->script)) {
        // Sword-wave objects resolve through their own rising collision
        // windows; do not apply the generic cast-deadline casualty batch.
        battle.effect_applied = 1;
        return;
    }
    if(original::lightning_strike_count(skill->script)) {
        // MAGIC01 3900 picks min(skill tier, live enemy soldiers) handles,
        // then each GLOBAL 2506 child waits RNG(40,70) before its collision
        // object is placed on that target. Picks are made before any child
        // delay expires, so duplicate handles can waste a strike.
        const int target_side = 1-side;
        int live_count = 0;
        for(int i = 0; i < 200; ++i)
            if(battle.soldiers[target_side*200+i].hp > 0) ++live_count;
        const int target_strikes = minimum(original::lightning_strike_count(skill->script),live_count);
        const int fallback_strikes = original::lightning_strike_count(skill->script)-target_strikes;
        int targets[44];
        uint8_t target_sequences[44], target_delays[44];
        int target_min_x=battlefield_width, target_max_x=0;
        for(int strike = 0; strike < target_strikes; ++strike) {
            const int rank = roll(live_count);
            int current = 0;
            int selected = 0;
            for(int i = 0; i < 200; ++i) {
                if(battle.soldiers[target_side*200+i].hp <= 0) continue;
                if(current++ == rank) { selected = i; break; }
            }
            targets[strike] = selected;
            const int target_x=battle.soldiers[target_side*200+selected].x;
            target_min_x=minimum(target_min_x,target_x);
            target_max_x=maximum(target_max_x,target_x);
            // MAGIC01 3900 rolls one of sequence IDs 3910..3913 for each
            // child; adjacent sequence pairs use the two A3/A4 images.
            target_sequences[strike]=uint8_t(roll(4));
        }
        int fallback_x[44], fallback_y[44];
        uint8_t fallback_sequences[44], fallback_delays[44];
        if(fallback_strikes) {
            // After the selected-actor children, MAGIC01 3900 fills any
            // unused tier strikes with GLOBAL2507 objects at random x/y.
            // Its x range is the selected actors' extrema, widened by 320
            // native pixels if they coincide; with no soldiers it uses the
            // opposing general as the center of that range.
            int min_native_x, max_native_x;
            if(target_strikes) {
                min_native_x=target_min_x*96/battle_grid_column_width;
                max_native_x=target_max_x*96/battle_grid_column_width;
            } else {
                min_native_x=max_native_x=int(enemy.x)*96/battle_grid_column_width;
            }
            if(min_native_x==max_native_x) {
                min_native_x-=320;
                max_native_x+=320;
            }
            for(int strike=0;strike<fallback_strikes;++strike) {
                const int native_x=min_native_x+roll(max_native_x-min_native_x+1);
                const int native_y=150+roll(501);
                fallback_x[strike]=native_x*battle_grid_column_width/96;
                fallback_y[strike]=native_y*battle_grid_row_height/72;
                fallback_sequences[strike]=uint8_t(roll(4));
            }
        }
        // The GLOBAL2506/2507 children draw their independent 40..70 waits
        // after the parent has queued the selected-target and fallback lists.
        for(int strike=0;strike<target_strikes;++strike)
            target_delays[strike]=uint8_t(40+roll(31));
        for(int strike=0;strike<fallback_strikes;++strike)
            fallback_delays[strike]=uint8_t(40+roll(31));
        for(int strike = 0; strike < target_strikes; ++strike) {
            Projectile* bolt = nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                bolt = &projectile;
                break;
            }
            if(!bolt) break;
            const auto& target = battle.soldiers[target_side*200+targets[strike]];
            *bolt = {};
            bolt->active = 1;
            bolt->magic = 6;
            bolt->kind = target_sequences[strike];
            bolt->phase = 0;
            bolt->remaining = target_delays[strike];
            bolt->x = target.x;
            bolt->y = target.y;
            bolt->target_side = int8_t(target_side);
            bolt->owner_index = uint8_t(targets[strike]);
            bolt->source_script = int16_t(skill->script);
        }
        for(int strike=0;strike<fallback_strikes;++strike) {
            Projectile* bolt=nullptr;
            for(auto& projectile : battle.projectiles) if(!projectile.active) {
                bolt=&projectile;
                break;
            }
            if(!bolt) break;
            *bolt={};
            bolt->active=1;
            bolt->magic=6;
            bolt->kind=fallback_sequences[strike];
            bolt->phase=0;
            bolt->remaining=fallback_delays[strike];
            bolt->x=int16_t(fallback_x[strike]);
            bolt->y=int16_t(fallback_y[strike]);
            bolt->target_side=int8_t(target_side);
            bolt->owner_index=0xff; // fixed-coordinate GLOBAL2507 strike
            // GLOBAL2507's native17 argument is global36 orientation; the
            // source #8 holds its first M009 image from object creation.
            bolt->scatter=0;
            bolt->dx=int16_t(side==0 ? -1 : 1);
            bolt->source_script=int16_t(skill->script);
        }
        battle.effect_applied = 1;
        return;
    }
    if(original::magic_action_delay(skill->script)) {
        if(skill->script==2650) {
            // GLOBAL4700 runs floor(live_count/2)+1 pairs of native269
            // selections and retains the extreme sampled x: max for side0,
            // min for side1. It then shifts the anchor by -320/+320 native
            // units respectively before placing the four-by-two array.
            const int target_side=1-side;
            const int live_count=maximum(0,int(enemy.soldiers));
            int anchor_target_x=enemy.x;
            if(live_count) {
                const int pair_count=live_count/2+1;
                bool sampled=false;
                for(int pair=0;pair<pair_count;++pair) for(int draw=0;draw<2;++draw) {
                    int rank=roll(live_count);
                    int sampled_x=enemy.x;
                    for(int slot=0;slot<200;++slot) {
                        const auto& soldier=battle.soldiers[target_side*200+slot];
                        if(soldier.hp<=0) continue;
                        if(rank--==0) { sampled_x=soldier.x; break; }
                    }
                    if(!sampled || (side==0 ? sampled_x>anchor_target_x :
                                               sampled_x<anchor_target_x))
                        anchor_target_x=sampled_x;
                    sampled=true;
                }
            }
            const int native_side_offset=side==0 ? -320 : 320;
            const int anchor_x=clamp(anchor_target_x+
                native_side_offset*battle_grid_column_width/96,
                                     0,battlefield_width-1);
            static constexpr int16_t native_dx[4]={-240,-80,80,240};
            for(int i=0;i<8;++i) {
                Projectile* marker=nullptr;
                for(auto& candidate:battle.projectiles) if(!candidate.active) {
                    marker=&candidate;
                    break;
                }
                if(!marker) break;
                *marker={};
                marker->active=1;
                marker->magic=12;
                marker->phase=0; // GLOBAL2509's independent 40..70 setup wait.
                marker->remaining=uint8_t(40+roll(31));
                marker->source_script=2650;
                marker->target_side=int8_t(1-side);
                marker->kind=uint8_t(i);
                marker->x=int16_t(clamp(anchor_x+
                    native_dx[i&3]*battle_grid_column_width/96,0,battlefield_width-1));
                // The native four-by-two field is 480 native units tall,
                // wider than the GBA battle viewport. Preserve the established
                // compact glyph bounds at y48..80 and y104..136, while the
                // sequence origin is the glyph image's top-left (Space=0).
                marker->y=int16_t(clamp(battle_commander_y+
                    ((i<4) ? 12 : -44),0,battle_grid_max_y));
                // GLOBAL2509 waits var26=300, then native38(16,1) gives the
                // static Things object its final sixteen-update timer.
                marker->height=316; // 300-update hold plus native38(16,1).
            }
        }
        // GLOBAL 2551 applies native 289 to each opposing battlefield actor.
        // FUN_0043be22 accumulates duration*32 at actor+0x3c; event 0x1000d
        // transfers that delay to the actor's action countdown when it expires.
        const uint16_t delay = uint16_t(original::magic_action_delay(skill->script));
        enemy.action_delay = uint16_t(minimum(65535,int(enemy.action_delay)+delay));
        for(int i = 0; i < 200; ++i) {
            auto& soldier = battle.soldiers[(1-side)*200+i];
            if(soldier.hp > 0)
                soldier.action_delay = uint16_t(minimum(65535,int(soldier.action_delay)+delay));
        }
        battle.effect_applied = 1;
        return;
    }
    const int scaled_damage = int(skill->attack) + int(skill->attack) * level(f.general) / 100;
    switch(skill->effect) {
    case 1: f.hp = int16_t(minimum(max_hp(f.general), int(f.hp) + original::healing_amount(skill->script))); break;
    case 2: {
        int add = minimum(original::reinforcement_amount(skill->script),maximum(0,original::reinforcement_limit(level(f.general))-int(f.soldiers)));
        const int roster_index = side ? int(battle.defender_index) : int(battle.attacker_index);
        const int layout = int(campaign.officers[f.general].formation) +
            (battle.layout_front[side][roster_index] ? 7 : 0);
        const int front_limit = front_capacity(f.general);
        const int rear_limit = rear_capacity(f.general);
        int front_rank = int(f.front);
        int rear_rank = int(f.soldiers)-front_rank;
        int entry_left=battlefield_width,entry_right=0,entry_top=battle_grid_max_y,entry_bottom=0;
        auto cell_occupied = [&](int column,int row) {
            for(const auto& general : battle.sides)
                if(general.hp>0 &&
                   (int(general.x)+battle_grid_column_width/2)/battle_grid_column_width==column &&
                   row==battle_commander_row) return true;
            for(const auto& soldier : battle.soldiers) if(soldier.hp>0) {
                if((int(soldier.x)+battle_grid_column_width/2)/battle_grid_column_width==column &&
                   (int(soldier.y)+battle_grid_row_height/2)/battle_grid_row_height==row) return true;
                if(soldier.move_ticks &&
                   (int(soldier.move_target_x)+battle_grid_column_width/2)/battle_grid_column_width==column &&
                   (int(soldier.move_target_y)+battle_grid_row_height/2)/battle_grid_row_height==row) return true;
            }
            return false;
        };
        auto place_at_native_cell = [&](int column,int row) {
            if(add<=0 || cell_occupied(column,row)) return;
            int slot=-1;
            for(int i=0;i<200;++i)
                if(battle.soldiers[side*200+i].hp<=0) { slot=i; break; }
            if(slot<0) { add=0; return; }
            // demake fills missing cavalry first, then missing infantry.
            const bool rear=rear_rank<rear_limit;
            const int rank_index=rear ? rear_rank : front_rank;
            if((rear && rear_rank>=rear_limit) || (!rear && front_rank>=front_limit)) {
                add=0;
                return;
            }
            uint8_t formation_column=0;
            (void)formation_soldier_point(layout,rear,rank_index,front_limit,rear_limit,
                                          &formation_column);
            const int world_x=column*battle_grid_column_width;
            const int world_y=row*battle_grid_row_height;
            auto& soldier=battle.soldiers[side*200+slot];
            soldier={};
            soldier.side=uint8_t(side);
            soldier.index=uint8_t(slot);
            soldier.attack_direction=int8_t(side ? 1 : -1);
            soldier.hp=1;
            soldier.x=soldier.previous_x=soldier.move_target_x=int16_t(world_x);
            soldier.y=soldier.previous_y=soldier.move_target_y=int16_t(world_y);
            soldier.rear=uint8_t(rear);
            soldier.formation_column=formation_column;
            entry_left=minimum(entry_left,world_x);entry_right=maximum(entry_right,world_x);
            entry_top=minimum(entry_top,world_y);entry_bottom=maximum(entry_bottom,world_y);
            battle.soldier_visual[side*200+slot]={uint8_t(side!=0),0,uint8_t(slot%3),uint8_t((slot/3)&3)};
            ++f.soldiers;
            if(rear) ++rear_rank;
            else { ++front_rank; ++f.front; }
            --add;
        };
        const int before=f.soldiers;
        // demake rear-edge columns, ten soldiers per column. Search inward
        // only when occupied, keeping both sides mirrored and cells distinct.
        for(int column=2;column<12 && add>0;++column)
            for(int row=2;row<12 && add>0;++row)
                place_at_native_cell(side ? column : battle_grid_columns-column,row);
        for(auto& marker:battle.projectiles)
            if(marker.active && marker.magic==36 && marker.target_side==side) {
                if(f.soldiers==before) marker.active=0;
                else {
                    marker.x=int16_t((entry_left+entry_right)/2);
                    marker.y=int16_t((entry_top+entry_bottom)/2);
                    marker.target_offset_x=int16_t(entry_right-entry_left+64);
                    marker.height=int16_t(entry_bottom-entry_top+64);
                    marker.kind=uint8_t(f.soldiers-before);
                }
            }
        if(f.soldiers==before) notice="兵力已满";
        break;
    }
    case 3:
        if(original::magic_target(skill->script) == original::MagicTarget::enemy_general) {
            // MAGIC01 3902 creates an effect without a battle-unit link, so
            // Five Thunder's direct FUN_0043f80f component is zero.
            int direct_damage = 0;
            if(skill->script != 2626) {
                // Linked general attacks use (STR/2 + RNG(STR-STR/2))/10.
                const int attack_str = strength(f.general);
                direct_damage = (attack_str / 2 + roll(attack_str - attack_str / 2)) / 10;
            }
            const int hit_damage = direct_damage + original::magic_general_hit_bonus(skill->script);
            enemy.hp = int16_t(maximum(0, int(enemy.hp) - hit_damage));
        }
        break;
    default: {
        const int casualties_count = 8 + skill->attack / 2;
        switch(original::magic_target(skill->script)) {
        case original::MagicTarget::enemy_general:
            enemy.hp = int16_t(maximum(0, int(enemy.hp) - scaled_damage));
            break;
        case original::MagicTarget::enemy_soldiers:
            casualties(1 - side, casualties_count);
            break;
        case original::MagicTarget::both_soldiers:
            casualties(1 - side, casualties_count);
            casualties(side, casualties_count);
            break;
        case original::MagicTarget::enemy_force:
        default:
            casualties(1 - side, casualties_count);
            enemy.hp = int16_t(maximum(0, int(enemy.hp) - scaled_damage));
            break;
        }
        break;
    }
    }
    battle.effect_applied = 1;
}


void Game::apply_fighter(int side) {
    const auto& f = battle.sides[side];
    auto& o = campaign.officers[f.general];
    o.hp = uint16_t(maximum(0, int(f.hp)));
    o.mp = uint16_t(minimum(maximum(0, int(f.mp)),max_mp(f.general)));
    o.soldiers = uint16_t(clamp(int(f.soldiers), 0, max_soldiers(f.general)));
    o.front = uint16_t(clamp(int(f.front),0,front_capacity(f.general)));
}

int Game::battle_casualty_experience(int side, int roster_index) const {
    if(side < 0 || side > 1) return 0;
    const int count = side == 0 ? battle.attacker_count : battle.defender_count;
    if(roster_index < 0 || roster_index >= count) return 0;
    const int general = side == 0 ? battle.attackers[roster_index] : battle.defenders[roster_index];
    if(general < 0 || general >= general_count) return 0;
    const auto& officer = campaign.officers[general];
    const int front_losses = maximum(0,int(battle.starting_front[side][roster_index])-int(officer.front));
    const int rear_now = int(officer.soldiers)-int(officer.front);
    const int rear_losses = maximum(0,int(battle.starting_rear[side][roster_index])-rear_now);
    return front_losses*6 + rear_losses*12;
}

void Game::add_battle_experience(int side, int roster_index, int amount) {
    if(side < 0 || side > 1 || amount <= 0) return;
    const int count = side == 0 ? battle.attacker_count : battle.defender_count;
    if(roster_index < 0 || roster_index >= count) return;
    battle.experience_rewards[side][roster_index] += uint32_t(amount);
}

void Game::distribute_battle_experience() {
    for(int side = 0; side < 2; ++side) {
        const int count = side == 0 ? battle.attacker_count : battle.defender_count;
        const int16_t* roster = side == 0 ? battle.attackers : battle.defenders;
        for(int i = 0; i < count; ++i) {
            const uint32_t reward = battle.experience_rewards[side][i];
            battle.experience_rewards[side][i] = 0;
            if(reward) {
                const int king = campaign.officers[roster[i]].king;
                if(king >= 0 && king < max_kings) campaign.faction_experience[king] += reward;
                gain_experience(roster[i],int(reward));
            }
        }
    }
}

[[gnu::noinline]] bool demake_battle_capture(Game& game,int loser_id,int winner_king,bool escaped) {
    auto& defeated=game.campaign.officers[loser_id];
    const int escape_count=native_escape_count(game.campaign,loser_id);
    if(!escaped) {
        int horse=defeated.equipped_item>=39 && defeated.equipped_item<=42 ? 1 : 0;
        if(defeated.king>=0 && defeated.king<scenario_defs[game.campaign.scenario].king_count &&
           king_defs[game.campaign.scenario][defeated.king].general==loser_id) {
            for(int city=0;city<city_count;++city)
                if(game.campaign.cities[city].king==defeated.king) { horse+=3; break; }
        }
        // SelectGeneralToWarController.WarResult captures only when the
        // demake random roll is strictly greater than this escape-adjusted cap.
        const int capture_limit=50+horse*25-int(escape_count)*25;
        if(game.roll(100)>capture_limit) {
            defeated.status=Status::prisoner;
            defeated.king=int8_t(winner_king);
            defeated.army=-1;
            defeated.city=game.battle.city;
            defeated.loyalty=30;
            native_status2_countdown(game.campaign,loser_id)=general_defs[loser_id].constancy;
            set_native_escape_count(game.campaign,loser_id,0);
            if(game.battle.captured_count<max_battle_captured)
                game.battle.captured[game.battle.captured_count++]=int16_t(loser_id);
            return true;
        }
    }
    if(escape_count<7) set_native_escape_count(game.campaign,loser_id,escape_count+1);
    return false;
}

void Game::resolve_duel(int winner) {
    int result_winner=-1,result_loser=-1;
    uint8_t result_kind=0;
    uint8_t result_level_before=0;
    auto next_round_or_finish = [&]() {
        if(!battle.roster_active_count[0] || !battle.roster_active_count[1]) {
            const int result = !battle.roster_active_count[0] && !battle.roster_active_count[1] ? 2 :
                !battle.roster_active_count[0] ? 1 : 0;
            finish_battle(result);
        } else {
            battle.attacker_index = int8_t(select_active_battle_general(0));
            battle.defender_index = int8_t(select_active_battle_general(1));
            battle.pending_winner = -1;
            battle.row_setup_active = 1;
            battle.round_result_winner=int16_t(result_winner);
            battle.round_result_loser=int16_t(result_loser);
            battle.round_result_kind=result_kind;
            battle.round_result_level_before=result_level_before;
            battle.round_result_pending=1;
        }
    };
    // demake's OnTimeUp sets warResult=2 and reloads SelectGeneralToWar. Its
    // warResult==2 branch leaves both fail flags untouched, keeps the player's
    // selected general, and picks a fresh opponent. A timed-out or double-KO
    // matchup therefore returns to lineup selection; it cannot consume both
    // generals or turn the whole siege into a draw.
    if(winner == 2) {
        apply_fighter(0);
        apply_fighter(1);
        int controlled_side=0;
        if(battle.army>=0 && battle.army<max_armies &&
           campaign.armies[battle.army].king==campaign.player) {
            controlled_side=0;
        } else if(battle.field_battle && battle.opponent_army>=0 &&
                  battle.opponent_army<max_armies &&
                  campaign.armies[battle.opponent_army].king==campaign.player) {
            controlled_side=1;
        } else if(!battle.field_battle && battle.city>=0 && battle.city<city_count &&
                  campaign.cities[battle.city].king==campaign.player) {
            controlled_side=1;
        }
        const int opponent_side=1-controlled_side;
        const int opponent_count=opponent_side==0 ? int(battle.attacker_count) : int(battle.defender_count);
        int active_count=0;
        for(int i=0;i<opponent_count;++i) active_count+=battle.roster_active[opponent_side][i] ? 1 : 0;
        if(active_count>0) {
            int selected=roll(active_count);
            for(int i=0;i<opponent_count;++i) {
                if(!battle.roster_active[opponent_side][i]) continue;
                if(selected--==0) {
                    if(opponent_side==0) battle.attacker_index=int8_t(i);
                    else battle.defender_index=int8_t(i);
                    break;
                }
            }
        }
        battle.pending_winner=-1;
        battle.row_setup_active=1;
        battle.round_result_pending=0;
        battle.round_result_winner=-1;
        battle.round_result_loser=-1;
        battle.round_result_kind=0;
        return;
    }
    int loser = 1 - winner;
    apply_fighter(0);
    apply_fighter(1);
    // SelectGeneralToWarController.WarResult awards the winner immediately,
    // before its capture roll. Level-up RNG and the next matchup must observe
    // that new level; native casualty/loser rewards are not used by demake.
    const int victor=battle.sides[winner].general;
    const int victor_level=level(victor);
    result_level_before=uint8_t(victor_level);
    const uint32_t duel_reward=(demake_level_experience(victor_level+1)-demake_level_experience(victor_level))/2;
    const int victor_king=campaign.officers[victor].king;
    if(victor_king>=0 && victor_king<max_kings) campaign.faction_experience[victor_king]+=duel_reward;
    if(duel_reward) gain_experience(victor,int(duel_reward));
    const bool escaped=battle.sides[loser].hp>0 &&
        (battle.sides[loser].ai_action_flags&fighter_escaped_flag);
    const bool captured_this_duel=demake_battle_capture(
        *this,battle.sides[loser].general,campaign.officers[battle.sides[winner].general].king,escaped);
    result_winner=battle.sides[winner].general;
    result_loser=battle.sides[loser].general;
    result_kind=captured_this_duel ? 2 : escaped ? 4 : 1;
    const int loser_index = loser == 0 ? battle.attacker_index : battle.defender_index;
    battle.roster_active[loser][loser_index] = 0;
    if(battle.roster_active_count[loser] > 0) --battle.roster_active_count[loser];
    next_round_or_finish();
}

void Game::finish_battle(int winner,bool native_auto) {
    auto& a = campaign.armies[battle.army];
    const bool player_battle = a.king == campaign.player ||
        (battle.field_battle ? campaign.armies[battle.opponent_army].king == campaign.player :
                              campaign.cities[battle.city].king == campaign.player);
    // Capture changes Officer::king during the duel. Keep the original side's
    // roster, then report its final settled states rather than searching the
    // player's current faction list (which no longer contains captured men).
    const bool reportable = player_battle && (battle.attacker_count || battle.defender_count);
    if(reportable) {
        battle_report = {};
        const int own_side = a.king == campaign.player ? 0 : 1;
        battle_report.result = uint8_t(winner == 2 ? 2 : winner == own_side ? 0 : 1);
        const int16_t* roster = own_side == 0 ? battle.attackers : battle.defenders;
        const int count = own_side == 0 ? battle.attacker_count : battle.defender_count;
        battle_report.count = uint8_t(minimum(count,max_battle_generals));
        for(int i=0;i<battle_report.count;++i) battle_report.entries[i].general = roster[i];
    }
    auto complete_report = [&]() {
        if(!reportable) return;
        for(int i=0;i<battle_report.count;++i) {
            auto& entry = battle_report.entries[i];
            const auto& officer = campaign.officers[entry.general];
            entry.king = officer.king;
            entry.city = officer.city;
            if(officer.status == Status::prisoner && officer.king != campaign.player) {
                entry.outcome = BattleOfficerOutcome::captured;
                ++battle_report.captured_count;
            } else if(officer.status != Status::employed || officer.king != campaign.player) {
                entry.outcome = BattleOfficerOutcome::scattered;
            } else if(officer.army >= 0 && officer.army < max_armies) {
                const auto& army = campaign.armies[officer.army];
                entry.city = army.retreating == 2 ? army.retreat_goal : army.target;
                entry.outcome = army.retreating ? BattleOfficerOutcome::retreating : BattleOfficerOutcome::marching;
            } else {
                entry.outcome = BattleOfficerOutcome::stationed;
            }
            if(officer.status == Status::employed && officer.king == campaign.player) {
                for(int j=0;j<battle.captured_count;++j)
                    if(battle.captured[j] == entry.general) entry.outcome = BattleOfficerOutcome::rescued;
            }
        }
        battle_report.pending = 1;
    };
    auto settle_retreat_funds = [&](Army& army) {
        bool has_survivor = false;
        for(int i = 0; i < army.count; ++i)
            has_survivor |= campaign.officers[army.generals[i]].status == Status::employed;
        if(!has_survivor) {
            army.money = 0;
            return;
        }
        army.money = scale_32(army.money,uint32_t(4000 + roll(2000)),10000u);
    };
    auto select_field_retreat_goal = [&](const Army& army,int survivor_count,int route_origin) {
        auto has_capacity = [&](int city) {
            if(city < 0 || city >= city_count) return false;
            int stationed[general_count];
            const int city_count_here = officers_at(city,campaign.cities[city].king,stationed,general_count);
            return city_count_here + survivor_count < 11;
        };
        // FUN_0040144e first tries the opposite endpoint of the current route;
        // FUN_00401416 requires its roster plus the retreating group to stay < 11.
        if(has_capacity(route_origin)) return route_origin;

        // Native FUN_004010c6 first samples neighbors sharing the local city's
        // owner (including neutral cities), then neutral neighbors, then a
        // shuffled all-owner neighbor list. GBA stores city IDs and ownership
        // but not the native route/leader links, so use the route origin as the
        // local city and preserve those owner-priority passes.
        auto choose_random = [&](const int* candidates,int count) {
            return count > 0 ? candidates[roll(count)] : -1;
        };
        auto shuffle_native = [&](int* candidates,int count) {
            // FUN_00401065 swaps each slot with a uniformly selected slot in
            // the whole list; this intentionally differs from Fisher-Yates.
            for(int i = 0; i < count; ++i) {
                const int j = roll(count);
                const int tmp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = tmp;
            }
        };
        int candidates[city_count],count = 0;
        if(route_origin >= 0 && route_origin < city_count) {
            const int local_owner = campaign.cities[route_origin].king;
            for(int slot = 0; slot < city_neighbor_counts[route_origin]; ++slot) {
                const int city = city_neighbor_order[route_origin][slot];
                if(
                   (campaign.cities[city].king == local_owner || campaign.cities[city].king < 0))
                    candidates[count++] = city;
            }
        }
        int goal = choose_random(candidates,count);
        if(goal >= 0) return goal;

        count = 0;
        if(route_origin >= 0 && route_origin < city_count)
            for(int slot = 0; slot < city_neighbor_counts[route_origin]; ++slot) {
                const int city = city_neighbor_order[route_origin][slot];
                if(campaign.cities[city].king < 0) candidates[count++] = city;
            }
        goal = choose_random(candidates,count);
        if(goal >= 0) return goal;

        count = 0;
        if(route_origin >= 0 && route_origin < city_count)
            for(int slot = 0; slot < city_neighbor_counts[route_origin]; ++slot)
                candidates[count++] = city_neighbor_order[route_origin][slot];
        shuffle_native(candidates,count);
        goal = count > 0 ? candidates[0] : -1;
        if(goal >= 0) return goal;

        // FUN_0040127e falls back to the retreating force's linked city list.
        count = 0;
        for(int city = 0; city < city_count; ++city)
            if(campaign.cities[city].king == army.king)
                candidates[count++] = city;
        goal = choose_random(candidates,count);
        if(goal >= 0) return goal;

        // The final native fallback samples its eligible world city records.
        count = 0;
        for(int city = 0; city < city_count; ++city)
            candidates[count++] = city;
        return choose_random(candidates,count);
    };
    auto begin_retreat = [&](Army& army,int survivor_count,int old_origin,int old_target,uint16_t old_remaining) {
        const int goal = select_field_retreat_goal(army,survivor_count,old_origin);
        if(goal < 0 || old_origin < 0 || old_origin >= city_count ||
           old_target < 0 || old_target >= city_count) return false;
        const int total = maximum(1,int(army.total));
        const int progress = total - minimum(total,int(old_remaining));
        army.origin = int8_t(old_target);
        army.target = int8_t(old_origin);
        army.total = uint16_t(total);
        army.remaining = uint16_t(progress);
        army.retreat_goal = int8_t(goal);
        army.retreating = 1;
        army.garrisoned=0;
        const int index=&army-campaign.armies;
        army_pending_destination(campaign,index)=((city_neighbors[old_origin]>>old_target)&1) &&
            !army_reverse_geometry(campaign,index) ? 0x80 : 0;
        return true;
    };
    auto return_or_capture = [&](Army& army,int old_origin,int old_target,
                                 uint16_t old_remaining,int captor) {
        int16_t survivors[max_army_generals];
        int survivor_count = 0;
        const int army_index = &army-campaign.armies;
        for(int i = 0; i < army.count; ++i) {
            auto& officer = campaign.officers[army.generals[i]];
            if(officer.status == Status::employed) survivors[survivor_count++] = army.generals[i];
            else {
                officer.army = -1;
                officer.city = -1;
                if(officer.status != Status::prisoner && captor >= 0) {
                    officer.status = Status::prisoner;
                    officer.king = int8_t(captor);
                    native_status2_countdown(campaign,army.generals[i]) =
                        general_defs[army.generals[i]].constancy;
                }
            }
        }
        if(survivor_count && begin_retreat(army,survivor_count,old_origin,old_target,old_remaining)) {
            army.count = uint8_t(survivor_count);
            for(int i = 0; i < survivor_count; ++i) {
                army.generals[i] = survivors[i];
                auto& officer = campaign.officers[survivors[i]];
                officer.army = int16_t(army_index);
                officer.city = -1;
            }
            return true;
        }
        for(int i = 0; i < survivor_count; ++i) {
            auto& officer = campaign.officers[survivors[i]];
            officer.army = -1;
            officer.city = -1;
            if(captor >= 0) {
                officer.status = Status::prisoner;
                officer.king = int8_t(captor);
                native_status2_countdown(campaign,survivors[i]) =
                    general_defs[survivors[i]].constancy;
            } else {
                officer.status = Status::wild;
                officer.king = -1;
            }
        }
        army.count = 0;
        army.active = 0;
        army.money = 0;
        return false;
    };
    auto release_transport_prisoners = [&](int army_index,int city,int captor) {
        for(auto& officer : campaign.officers) {
            if(officer.status != Status::prisoner || officer.army != army_index || officer.city != -1) continue;
            officer.army = -1;
            officer.city = int16_t(city);
            officer.king = int8_t(captor);
        }
    };
    auto transfer_transport_prisoners = [&](int from_army,int to_army,int captor) {
        for(auto& officer : campaign.officers) {
            if(officer.status != Status::prisoner || officer.army != from_army || officer.city != -1) continue;
            officer.army = int16_t(to_army);
            officer.king = int8_t(captor);
        }
    };
    if(winner == 2) {
        // Per-pair draw rewards were already recorded by resolve_duel.
        distribute_battle_experience();
        const int a_origin = a.origin, a_target = a.target;
        const uint16_t a_remaining = a.remaining;
        settle_retreat_funds(a);
        return_or_capture(a,a_origin,a_target,a_remaining,
                          battle.field_battle ? campaign.armies[battle.opponent_army].king : campaign.cities[battle.city].king);
        if(battle.field_battle) {
            auto& b = campaign.armies[battle.opponent_army];
            const int b_origin = b.origin, b_target = b.target;
            const uint16_t b_remaining = b.remaining;
            settle_retreat_funds(b);
            return_or_capture(b,b_origin,b_target,b_remaining,a.king);
            if(!a.active && b.active)
                transfer_transport_prisoners(battle.army,battle.opponent_army,b.king);
            if(!b.active && a.active)
                transfer_transport_prisoners(battle.opponent_army,battle.army,a.king);
            if(!a.active && !b.active) {
                int a_city = nearest_owned_city(campaign,a.king,a_origin);
                int b_city = nearest_owned_city(campaign,b.king,b_origin);
                if(a_city < 0) a_city = clamp(a_origin,0,city_count-1);
                if(b_city < 0) b_city = clamp(b_origin,0,city_count-1);
                if(a_city >= 0) release_transport_prisoners(battle.army,a_city,a.king);
                if(b_city >= 0) release_transport_prisoners(battle.opponent_army,b_city,b.king);
            }
        } else if(!a.active) {
            release_transport_prisoners(battle.army,battle.city,campaign.cities[battle.city].king);
        }
        battle.winner = 2;
        campaign.phase = Phase::world;
        if(player_battle) notice = "双方已无可出战武将，战斗平局";
        check_end();
        complete_report();
        return;
    }

    if(battle.field_battle && !player_battle) {
        // demake AttackArmy settles the winning and losing ArmyInfo records
        // after its sequential GeneralInfo matchups. Winners recover any of
        // their own officers captured in earlier pairings and earn the army
        // victory award. Captured losers join the winner's carried prisoners;
        // uncaptured losers retreat with their remaining roster and cargo.
        const int winning_index=winner==0 ? battle.army : battle.opponent_army;
        const int losing_index=winner==0 ? battle.opponent_army : battle.army;
        auto& winning_army=campaign.armies[winning_index];
        auto& losing_army=campaign.armies[losing_index];
        const int winning_king=winning_army.king;
        for(int i=0;i<winning_army.count;++i) {
            const int g=winning_army.generals[i]; auto& officer=campaign.officers[g];
            officer.status=Status::employed; officer.king=int8_t(winning_king);
            officer.city=-1; officer.army=int16_t(winning_index);
            const uint32_t award=level(g)>4 ?
                (demake_level_experience(level(g)+1)-demake_level_experience(level(g)))/4 : 50;
            if(award) gain_experience(g,int(award));
        }
        int survivors[max_army_generals],survivor_count=0;
        for(int i=0;i<losing_army.count;++i) {
            const int g=losing_army.generals[i]; auto& officer=campaign.officers[g];
            if(officer.status==Status::prisoner && officer.king==winning_king) {
                officer.army=int16_t(winning_index); officer.city=-1;
                continue;
            }
            survivors[survivor_count++]=g;
        }
        if(survivor_count) {
            const int old_origin=losing_army.origin,old_target=losing_army.target;
            const uint16_t old_remaining=losing_army.remaining;
            const int total=maximum(1,int(losing_army.total));
            const int progress=total-minimum(total,int(old_remaining));
            losing_army.count=uint8_t(survivor_count);
            for(int i=0;i<survivor_count;++i) {
                const int g=survivors[i]; losing_army.generals[i]=int16_t(g);
                campaign.officers[g].city=-1; campaign.officers[g].army=int16_t(losing_index);
            }
            losing_army.origin=int8_t(old_target); losing_army.target=int8_t(old_origin);
            losing_army.total=uint16_t(total); losing_army.remaining=uint16_t(progress);
            losing_army.retreat_goal=int8_t(old_origin); losing_army.retreating=2;
            losing_army.garrisoned=0;
            army_pending_destination(campaign,losing_index)=((city_neighbors[old_origin]>>old_target)&1) &&
                !army_reverse_geometry(campaign,losing_index) ? 0x80 : 0;
        } else {
            transfer_transport_prisoners(losing_index,winning_index,winning_king);
            winning_army.money+=losing_army.money;
            losing_army.money=0; losing_army.count=0; losing_army.active=0;
        }
        battle.winner=int8_t(winner); campaign.phase=Phase::world;
        check_end();
        return;
    }

    // Interactive demake battles grant each winning roster member 50 EXP at
    // levels 1-4, otherwise half a level span. Keep the native roster formula
    // only for the legacy non-player resolver, which has its own bookkeeping.
    const int winning_count = winner == 0 ? int(battle.attacker_count) : int(battle.defender_count);
    const int losing_count = winner == 0 ? int(battle.defender_count) : int(battle.attacker_count);
    if(player_battle) {
        const int16_t* roster=winner==0 ? battle.attackers : battle.defenders;
        const int faction=winner==0 ? a.king : battle.field_battle ?
            campaign.armies[battle.opponent_army].king : campaign.cities[battle.city].king;
        for(int i=0;i<winning_count;++i) {
            const int g=roster[i],lv=level(g);
            const uint32_t award=lv>4 ? (demake_level_experience(lv+1)-demake_level_experience(lv))/2 : 50;
            if(faction>=0 && faction<max_kings) campaign.faction_experience[faction]+=award;
            if(award) gain_experience(g,int(award));
        }
    } else if(winning_count > 0) {
        int opposing_levels = 0;
        const int16_t* opposing_roster = winner == 0 ? battle.defenders : battle.attackers;
        for(int i = 0; i < losing_count; ++i) opposing_levels += level(opposing_roster[i]);
        const int reward_per_winner = 32 * opposing_levels / winning_count;
        const int winning_side = winner == 0 ? 0 : 1;
        for(int i = 0; i < winning_count; ++i) add_battle_experience(winning_side,i,reward_per_winner);
    }
    distribute_battle_experience();

    if(battle.field_battle) {
        auto& b = campaign.armies[battle.opponent_army];
        const int losing_side = 1 - winner;
        const int losing_index = losing_side == 0 ? battle.army : battle.opponent_army;
        auto& losing_army = campaign.armies[losing_index];
        const int losing_origin = losing_army.origin;
        const int losing_target = losing_army.target;
        const uint16_t losing_remaining = losing_army.remaining;
        const int winning_king = (winner == 0 ? a : b).king;
        settle_retreat_funds(losing_army);
        return_or_capture(losing_army,losing_origin,losing_target,losing_remaining,winning_king);
        const int winning_index = winner == 0 ? battle.army : battle.opponent_army;
        auto& winning_army = campaign.armies[winning_index];
        if(!losing_army.active)
            transfer_transport_prisoners(losing_index,winning_index,winning_king);
        int prison_city = campaign.cities[winning_army.origin].king == winning_king ? winning_army.origin :
            nearest_owned_city(campaign,winning_king,winning_army.origin);
        for(int i = 0; i < battle.captured_count; ++i) {
            auto& officer = campaign.officers[battle.captured[i]];
            bool was_in_winning_army = false;
            for(int j = 0; j < winning_army.count; ++j)
                was_in_winning_army |= winning_army.generals[j] == battle.captured[i];
            officer.king = int8_t(winning_king);
            if(was_in_winning_army) {
                // A commander captured during an earlier duel is rescued if
                // the rest of their field army wins the encounter.
                officer.status = Status::employed;
                officer.army = int16_t(winning_index);
                officer.city = -1;
                native_origin_king(campaign,battle.captured[i]) = uint8_t(winning_king);
            } else {
                officer.army = -1;
                officer.city = int16_t(prison_city);
            }
        }
        battle.winner = int8_t(winner);
        campaign.phase = winning_king == campaign.player ? Phase::prisoners : Phase::world;
        if(player_battle) notice = winning_king == campaign.player ? "野战胜利，请处理俘虏" : "野战结束，胜方继续行军";
        check_end();
        complete_report();
        return;
    }
    const int target = a.target, origin = a.origin;
    const int former_city_king = campaign.cities[target].king;
    const int former_city_major = campaign.cities[target].major;
    const int attacking_king = a.king;
    int attacking_major = -1;
    for(int i = 0; i < a.count; ++i)
        if(campaign.officers[a.generals[i]].status == Status::employed) {
            attacking_major = a.generals[i];
            break;
        }
    // FUN_0040e7ed is called from the city-turn return path after an
    // interactive battle. Unlike the AI resolver, that path reaches here
    // without applying city attrition in resolve_ai_battle.
    if(!native_auto && former_city_king >= 0) {
        auto& city = campaign.cities[target];
        if(winner == 0) {
            // FUN_0040e13f: retain 30-50% of funds, lose 15-20% of
            // population and defense, and empty the reserve.
            city.money = scale_32(city.money,uint32_t(3000+roll(2000)),10000u);
            const uint32_t population_loss = scale_32(city.population,uint32_t(1500+roll(500)),10000u);
            const uint32_t defense_loss = (uint32_t(1500+roll(500))*city.defense)/10000u;
            city.population -= population_loss;
            city.defense = uint16_t(uint32_t(city.defense)-defense_loss);
            city.reserve = 0;
        } else if(winner == 1) {
            // FUN_0040e0c4: a defended city loses 5-7% of population
            // and defense, but keeps its funds and reserve.
            const uint32_t population_loss = scale_32(city.population,uint32_t(500+roll(200)),10000u);
            const uint32_t defense_loss = (uint32_t(500+roll(200))*city.defense)/10000u;
            city.population -= population_loss;
            city.defense = uint16_t(uint32_t(city.defense)-defense_loss);
        }
    }
    int evacuees[general_count], evacuee_count = 0;
    int evacuation_target = -1;
    if(winner == 0 && former_city_king >= 0) {
        for(int i = 0; i < general_count; ++i) {
            const auto& officer = campaign.officers[i];
            if(officer.city == target && officer.king == former_city_king &&
               officer.status == Status::employed)
                evacuees[evacuee_count++] = i;
        }
        // Keep the former ruler in the lead refugee army's first five, as
        // the native city roster does when building its grouped evacuation.
        for(int i = 0; i < evacuee_count; ++i) if(evacuees[i] == former_city_major) {
            const int first = evacuees[0];
            evacuees[0] = evacuees[i];
            evacuees[i] = first;
            break;
        }
        // Native FUN_004012cd checks adjacent cities in owner, neutral, then
        // other-owner order before it falls back to the former owner's list.
        auto choose_city = [&](int category) {
            int candidates[city_count], count = 0;
            for(int city = 0; city < city_count; ++city) {
                if(city == target || !((city_neighbors[target] >> city) & 1)) continue;
                const int owner = campaign.cities[city].king;
                if((category == 0 && owner == former_city_king) ||
                   (category == 1 && owner < 0) ||
                   (category == 2 && owner >= 0 && owner != former_city_king))
                    candidates[count++] = city;
            }
            return count ? candidates[roll(count)] : -1;
        };
        evacuation_target = choose_city(0);
        if(evacuation_target < 0) evacuation_target = choose_city(1);
        if(evacuation_target < 0) evacuation_target = choose_city(2);
        if(evacuation_target < 0) {
            int candidates[city_count], count = 0;
            for(int city = 0; city < city_count; ++city)
                if(city != target && campaign.cities[city].king == former_city_king)
                    candidates[count++] = city;
            if(count) evacuation_target = candidates[roll(count)];
        }
        if(evacuation_target < 0) {
            int candidates[city_count], count = 0;
            for(int city = 0; city < city_count; ++city)
                if(city != target && campaign.cities[city].king >= 0 &&
                   campaign.cities[city].king != former_city_king)
                    candidates[count++] = city;
            if(count) evacuation_target = candidates[roll(count)];
        }
    }
    const int evacuation_next_hop = evacuation_target >= 0 ?
        next_city_on_route(target,evacuation_target) : -1;
    const int winning_king = winner == 0 ? a.king : campaign.cities[target].king;
    if(winner == 0) {
        campaign.cities[target].king = a.king;
        campaign.cities[target].money += a.money;
        // Remaining defenders retreat; prisoners at the captured city transfer to its new owner.
        for(auto& o : campaign.officers) {
            if(o.city != target || o.king == a.king) continue;
            if(o.status == Status::prisoner) {
                o.king = a.king;
            } else if(o.status == Status::employed) {
                bool is_evacuee = false;
                const int general = &o-campaign.officers;
                for(int i = 0; i < evacuee_count; ++i) is_evacuee |= evacuees[i] == general;
                if(native_auto || is_evacuee) {
                    o.city = -1;
                    o.army = -1;
                } else {
                    int retreat_city = -1;
                    for(int i = 0; i < city_count; ++i) if(campaign.cities[i].king == o.king) { retreat_city = i; break; }
                    if(retreat_city >= 0) o.city = int16_t(retreat_city);
                    else {
                        o.status = Status::prisoner; o.king = a.king; o.loyalty = 30;
                        native_status2_countdown(campaign,&o-campaign.officers) =
                            general_defs[&o-campaign.officers].constancy;
                    }
                }
            }
        }
    }
    if(winner == 0) {
        for(int i = 0; i < a.count; ++i) {
            auto& o = campaign.officers[a.generals[i]];
            o.army = -1;
            if(o.status != Status::prisoner) {
                o.city = int16_t(target);
            }
        }
        release_transport_prisoners(battle.army,target,attacking_king);
        a.active = 0;
        // FUN_00438b36 appoints the incoming leader when the captured city
        // has no employed garrison, including an empty neutral city.
        if(attacking_major >= 0) campaign.cities[target].major = int16_t(attacking_major);
        if(former_city_king >= 0) {
            bool ruler_in_evacuees = false;
            for(int i = 0; i < evacuee_count; ++i)
                ruler_in_evacuees |= evacuees[i] == former_city_major;
            int evacuee_index = 0;
            while(evacuee_index < evacuee_count && evacuation_next_hop >= 0) {
                int group_count = ruler_in_evacuees && evacuee_index == 0 ?
                    minimum(5,evacuee_count) : 1;
                int free_army = -1;
                for(int i = 0; i < max_armies; ++i)
                    if(!campaign.armies[i].active) { free_army = i; break; }
                if(free_army < 0) break;
                auto& refugee = campaign.armies[free_army];
                refugee = {};
                army_pending_destination(campaign,free_army)=0;
                refugee.count = uint8_t(group_count);
                refugee.king = int8_t(former_city_king);
                refugee.origin = int8_t(target);
                refugee.target = int8_t(evacuation_next_hop);
                refugee.total = city_travel_ticks(target,evacuation_next_hop);
                refugee.remaining = refugee.total;
                refugee.retreat_goal = int8_t(evacuation_target);
                refugee.retreating = 2;
                refugee.active = 1;
                for(int i = 0; i < group_count; ++i) {
                    const int general = evacuees[evacuee_index++];
                    refugee.generals[i] = int16_t(general);
                    auto& officer = campaign.officers[general];
                    officer.city = -1;
                    officer.army = int16_t(free_army);
                }
            }
            // `FUN_0040e13f` can create more refugee objects than the fixed
            // GBA army array has free slots. Keep any overflow under the new
            // city owner rather than leave detached officers in invalid state.
            while(evacuee_index < evacuee_count) {
                const int general = evacuees[evacuee_index++];
                auto& officer = campaign.officers[general];
                officer.status = Status::prisoner;
                officer.king = int8_t(attacking_king);
                officer.city = int16_t(target);
                officer.army = -1;
                officer.loyalty = 30;
                native_status2_countdown(campaign,general) = general_defs[general].constancy;
            }
        }
    } else {
        const uint16_t old_remaining = a.remaining;
        settle_retreat_funds(a);
        return_or_capture(a,origin,target,old_remaining,winning_king);
        if(!a.active) release_transport_prisoners(battle.army,target,winning_king);
    }
    // Captured officers stay with the final victor; no stranded prisoners in enemy cities.
    for(int i = 0; i < battle.captured_count; ++i) {
        auto& o = campaign.officers[battle.captured[i]];
        o.city = int16_t(target);
        o.king = int8_t(winning_king);
        native_status2_countdown(campaign,battle.captured[i]) =
            general_defs[battle.captured[i]].constancy;
    }
    battle.winner = int8_t(winner);
    campaign.phase = winning_king == campaign.player ? Phase::prisoners : Phase::world;
    if(player_battle) notice = winning_king == campaign.player ? "战斗胜利，请处理俘虏" : "战斗结束";
    check_end();
    complete_report();
}

void Game::handle_prisoner(int general, bool persuade) {
    if(general < 0 || general >= general_count) return;
    auto& o = campaign.officers[general];
    if(o.status != Status::prisoner || o.king != campaign.player) return;
    // The post-battle shortcut must not bypass demake's city/loyalty rules.
    if(persuade) { persuade_prisoner(general); return; }
    if(o.army>=0) {
        const auto& army=campaign.armies[o.army];
        o.city=int16_t(army.garrisoned || army.remaining*2<=army.total ? army.target : army.origin);
        o.army=-1;
    }
    o.status = Status::wild;
    o.king = -1;
    native_status2_countdown(campaign,general)=0;
    notice = "已释放俘虏";
}

const char* Game::prisoner_persuasion_block(int general) const {
    if(general<0 || general>=general_count) return "不可劝降";
    const auto& o=campaign.officers[general];
    if(o.status!=Status::prisoner || o.king!=campaign.player) return "不可劝降";
    if(o.city<0 || o.army>=0) {
        return "押运中";
    }
    return prisoner_allegiance(campaign,general,campaign.player)<0 ? "效忠原主" : nullptr;
}

bool Game::persuade_prisoner(int general) {
    if(const char* blocked=prisoner_persuasion_block(general)) {
        notice=blocked;
        if(general>=0 && general<general_count && campaign.officers[general].status==Status::prisoner &&
           campaign.officers[general].king==campaign.player) {
            if(campaign.officers[general].city<0 || campaign.officers[general].army>=0)
                notice="俘虏尚未押入城池，暂不能劝降";
            else notice="忠臣不事二主，原主仍在";
        }
        return false;
    }
    auto& o=campaign.officers[general];

    const int allegiance=prisoner_allegiance(campaign,general,campaign.player);
    const bool succeeds=allegiance>0 ||
        roll(100)<(100-int(o.loyalty))/2;
    if(!succeeds) {
        o.loyalty=uint8_t(clamp(int(o.loyalty)-5-roll(15),0,100));
        notice="劝降失败，忠诚下降";
        return false;
    }

    o.status=Status::employed;
    o.king=int8_t(campaign.player);
    o.loyalty=90;
    // SPController sets active=0 on persuasion. Joining is not a free
    // additional domestic action; the annual reset restores eligibility.
    o.worked=1;
    o.army=-1;
    o.soldiers=uint16_t(max_soldiers(general));
    o.front=uint16_t(front_capacity(general));
    native_origin_king(campaign,general)=uint8_t(campaign.player);
    native_status2_countdown(campaign,general)=0;
    notice="劝降成功";
    return true;
}

void Game::finish_prisoners() { if(campaign.phase == Phase::prisoners) campaign.phase = Phase::world; }

void Game::check_end() {
    // demake WarOverResult calls SetKingOver when the faction leader is taken
    // prisoner. Its neutral kingNum sentinel maps to -1 on GBA. The fixed GBA
    // army pool cannot represent neutral armies, so their officers return as
    // wild generals to the army's current city, retaining its money there.
    for(int king=0;king<scenario_defs[campaign.scenario].king_count;++king) {
        const int ruler=king_defs[campaign.scenario][king].general;
        if(ruler<0 || ruler>=general_count) continue;
        const auto& leader=campaign.officers[ruler];
        if(leader.status!=Status::prisoner || leader.king==king) continue;
        bool remains=false;
        for(const auto& city:campaign.cities) remains|=city.king==king;
        for(const auto& army:campaign.armies) remains|=army.active && army.king==king;
        if(!remains) continue;
        for(int i=0;i<max_armies;++i) {
            auto& army=campaign.armies[i];
            if(!army.active || army.king!=king) continue;
            const int city=army.garrisoned || army.remaining*2<=army.total ? army.target : army.origin;
            if(city>=0 && city<city_count) campaign.cities[city].money+=army.money;
            for(auto& officer:campaign.officers) if(officer.army==i) {
                officer.army=-1; officer.city=int16_t(city);
            }
            army.money=0; army.count=0; army.active=0; army.garrisoned=0;
        }
        for(auto& city:campaign.cities) if(city.king==king) { city.king=-1; city.major=-1; }
        for(int g=0;g<general_count;++g) {
            auto& officer=campaign.officers[g];
            if(officer.king!=king) continue;
            officer.king=-1;
            if(officer.status!=Status::absent) officer.status=Status::wild;
            native_status2_countdown(campaign,g)=0;
        }
    }
    bool player_alive = false, all_cities_unified = true;
    for(const auto& c : campaign.cities) {
        player_alive |= c.king == campaign.player;
        // Native FUN_00417c2b walks every city record. An unowned city also
        // blocks unification unless its native FLAGS field is 1. All shipped
        // scenarios load FLAGS as 0, so every city must belong to the player.
        all_cities_unified &= c.king == campaign.player;
    }
    for(const auto& a : campaign.armies) if(a.active) {
        player_alive |= a.king == campaign.player;
    }
    if(! player_alive || all_cities_unified) {
        campaign.phase = Phase::ended;
        notice = player_alive ? "统一天下" : "势力灭亡";
    }
}

bool Game::valid() const {
    const auto& c = campaign;
    if(c.scenario >= scenario_count || c.player >= scenario_defs[c.scenario].king_count || c.month < 1 || c.month > 12) return false;
    if(c.phase != Phase::domestic && c.phase != Phase::world && c.phase != Phase::battle && c.phase != Phase::prisoners && c.phase != Phase::ended) return false;
    if(c.year < scenario_defs[c.scenario].year || c.month_ticks >= original::month_updates) return false;
    int kings = scenario_defs[c.scenario].king_count;
    // demake reservistMax is fixed per city, including after siege population
    // losses. Match replenishment/transfer rules; the native population-based
    // formula can reject a legitimate long-running campaign and its save.
    for(int i=0;i<city_count;++i) {
        const auto& city=c.cities[i];
        if(city.king < -1 || city.king >= kings || city.reserve > original::demake_reserve_capacity(i) || city.major >= general_count) return false;
    }
    for(int i = 0; i < general_count; ++i) {
        const auto& o = c.officers[i];
        if(o.city < -1 || o.city >= city_count || o.king < -1 || o.king >= kings || o.army < -1 || o.army >= max_armies ||
           int(o.status) > 3 || o.force >= unit_types || o.formation >= 7 || o.rank > 50 || o.soldiers > max_soldiers(i) || o.front > o.soldiers || o.front > front_capacity(i) ||
           o.soldiers-o.front > rear_capacity(i) || o.loyalty > 100 || o.hp > max_hp(i) || o.mp > max_mp(i) || o.equipped_item >= 55 || (o.item_flags[2] & 0xfc)) return false;
        if((native_origin_king(c,i) != 0xff && native_origin_king(c,i) >= kings) ||
           native_status2_countdown(c,i) > general_defs[i].constancy) return false;

        if(o.army >= 0 && (! c.armies[o.army].active || o.city != -1)) return false;
    }
    for(int king=0;king<max_kings;++king) if(faction_inventory_count(c,king)>general_count*17) return false;
    int total_objects=0;
    for(int city=0;city<city_count;++city) {
        const int count=city_inventory_count(c,city);
        total_objects+=count;
        if(count>city_object_capacity) return false;
        for(int i=0;i<count;++i) {
            const uint8_t entry=city_inventory_item(c,city,i);
            const int type=city_inventory_type(entry),index=city_inventory_index(entry);
            if((type==0 && index>=55) || (type==1 && index>=31) ||
               (type==2 && index>=11) || (type==3 && index>=7)) return false;
        }
    }
    if(total_objects>faction_item_storage_bytes-city_count) return false;
    for(uint8_t used : c.item_used) if(used > 1) return false;
    for(int i = 0; i < max_armies; ++i) {
        const auto& a = c.armies[i];
        if(! a.active) continue;
        if(a.count < 1 || a.count > 5 || a.king < 0 || a.king >= kings || a.origin < 0 || a.origin >= city_count || a.target < 0 || a.target >= city_count || a.remaining > a.total || a.retreating > 2 || a.garrisoned > 1) return false;
        if(a.garrisoned && a.retreating) return false;
        if((faction_storage_byte(c,army_destination_storage+i)&0x7f)>city_count) return false;
        if(army_reverse_geometry(c,i) && !((city_neighbors[a.origin]>>a.target)&1)) return false;
        if((a.retreating && (a.retreat_goal < 0 || a.retreat_goal >= city_count)) ||
           (!a.retreating && a.retreat_goal != -1)) return false;
        if(!a.garrisoned && city_path_length(a.origin,a.target) < 0) return false;
        for(int j = 0; j < a.count; ++j) {
            int g = a.generals[j];
            if(g < 0 || g >= general_count || c.officers[g].army != i || c.officers[g].king != a.king || c.officers[g].status != Status::employed) return false;
            for(int k = 0; k < j; ++k) if(a.generals[k] == g) return false;
        }
    }
    return true;
}

uint32_t Game::checksum() const {
    uint32_t result = 2166136261u;
    const auto* data = reinterpret_cast<const uint8_t*>(&campaign);
    for(unsigned i = 0; i < sizeof(campaign); ++i) result = (result ^ data[i]) * 16777619u;
    return result;
}
}

