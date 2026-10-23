#include "model.h"
#include "original_rules.h"
#if defined(__arm__)
#include "bn_core.h"
extern "C" volatile uint32_t sg_probe[];
#endif

namespace sg {
namespace {
[[gnu::noinline]] void cancel_retreat_steps(Battle& battle) {
    for(int side=0;side<2;++side) {
        const auto& commander=battle.sides[side];
        bool needed=commander.order==Order::retreat || commander.order==Order::front_back;
        for(auto command:commander.ai_command_slots) needed|=command==8;
        if(!needed) continue;
        for(int i=side*200;i<(side+1)*200;++i) {
            auto& soldier=battle.soldiers[i];
            if(soldier.hp<=0 || !soldier.move_ticks) continue;
            const bool moving_back=commander.order==Order::retreat ||
                (commander.order==Order::front_back && soldier_in_front_rank(soldier)) ||
                (soldier.formation_column<8 && commander.ai_command_slots[soldier.formation_column]==8);
            if(!moving_back) continue;
            soldier.move_ticks=0;soldier.move_dx=soldier.move_dy=0;
            soldier.move_x_remainder=soldier.move_y_remainder=0;
            soldier.move_target_x=soldier.x;soldier.move_target_y=soldier.y;
        }
    }
}
// Keep the cold collision walk in ROM: battle_tick's IWRAM budget is tight.
[[gnu::noinline]] bool commander_lane_blocked(const Battle& battle,const int16_t* heads,const int16_t* next,int side,bool retreating) {
    constexpr int origin=battlefield_width/2-28*battle_grid_column_width;
    const int x=battle.sides[side].x;
    const int offset=x-origin+battle_grid_column_width/2;
    const int column=offset>=0 ? offset/battle_grid_column_width : -1;
    const int ahead=column+(side ? 1 : -1)*(retreating ? -1 : 1);
    const int row=(battle_commander_y+battle_grid_row_height/2)/battle_grid_row_height;
    for(int col=column-1;col<=column+1;++col) {
        if(unsigned(col)>=battle_grid_columns+1) continue;
        for(int index=heads[row*(battle_grid_columns+1)+col];index>=0;index=next[index]) {
            const auto& soldier=battle.soldiers[index];
            if(soldier.hp<=0) continue;
            if(soldier.side==side) { if(col==ahead) return true; }
            else if(!retreating && soldier.x>=x-battle_grid_column_width && soldier.x<=x+battle_grid_column_width) return true;
        }
    }
    return false;
}
#if defined(__arm__)
#define SANGO_MOVEMENT_HOT __attribute__((section(".iwram"),noinline))
#else
#define SANGO_MOVEMENT_HOT [[gnu::noinline]]
#endif
// Scratch storage is rebuilt for every update. It indexes reserved movement
// destinations separately from the current cells used by projectile hits.
class MovementOccupancy {
    static constexpr int columns=battle_grid_columns+1;
    static constexpr int cell_count=columns*battle_grid_rows;
    int16_t heads[columns*battle_grid_rows], next[400], cells[400];
    int16_t pursuit[2][cell_count], queue[cell_count];
    bool pursuit_ready[2]={};
    const Soldier* soldiers=nullptr;
    static int cell(int x,int y) {
        if(x<0 || y<0) return -1;
        const int column=(x+battle_grid_column_width/2)/battle_grid_column_width;
        const int row=(y+battle_grid_row_height/2)/battle_grid_row_height;
        return column<columns && row<battle_grid_rows ? row*columns+column : -1;
    }
public:
    [[gnu::noinline]] void reset(const Soldier* actors) {
        soldiers=actors;
        pursuit_ready[0]=pursuit_ready[1]=false;
        for(auto& head : heads) head=-1;
        for(int i=399; i>=0; --i) {
            const auto& actor=actors[i];
            const int destination=actor.hp>0 ? cell(actor.move_ticks ? actor.move_target_x : actor.x,
                                                   actor.move_ticks ? actor.move_target_y : actor.y) : -1;
            cells[i]=int16_t(destination); next[i]=-1;
            if(destination>=0) { next[i]=heads[destination]; heads[destination]=int16_t(i); }
        }
    }
    [[gnu::noinline]] bool occupied(int x,int y) const {
        const int destination=cell(x,y);
        if(destination<0) return false;
        for(int i=heads[destination]; i>=0; i=next[i]) {
            const auto& actor=soldiers[i];
            if(actor.hp>0) return true;
        }
        return false;
    }
    // Target occupants use logical cells. A firing actor still uses its
    // current position: a formation move can be queued during its attack.
    [[gnu::noinline]] int lane_target(int index,int direction,int max_distance,const Fighter* generals) const {
        const auto& actor=soldiers[index];
        const int start=cell(actor.x,actor.y);
        if(start<0) return -1;
        const int row=start/columns;
        const int start_column=start%columns;
        // demake's inclusive range includes the firing actor's own cell.
        for(int distance=0;distance<=max_distance;++distance) {
            const int column=start_column+direction*distance;
            if(column<0 || column>=battle_grid_columns) break;
            for(int candidate=heads[row*columns+column];candidate>=0;candidate=next[candidate])
                if(candidate!=index && soldiers[candidate].hp>0 &&
                   soldiers[candidate].side!=actor.side) return candidate;
            if(row==battle_commander_row) for(int side=0;side<2;++side)
                if(side!=actor.side && generals[side].hp>0 &&
                   (generals[side].x+battle_grid_column_width/2)/battle_grid_column_width==column)
                    return 400+side;
        }
        return -1;
    }
    SANGO_MOVEMENT_HOT int pursuit_target(int index,int side,const Fighter* generals) {
        if(!pursuit_ready[side]) {
            auto& field=pursuit[side];
            for(auto& target:field) target=-1;
            int end=0;
            auto seed=[&](int x,int y,int target) {
                const int location=cell(x,y);
                if(location>=0 && field[location]<0) { field[location]=int16_t(target); queue[end++]=int16_t(location); }
            };
            for(int i=(1-side)*200;i<(2-side)*200;++i) {
                const auto& candidate=soldiers[i];
                if(candidate.hp>0) seed(candidate.move_ticks ? candidate.move_target_x : candidate.x,
                                        candidate.move_ticks ? candidate.move_target_y : candidate.y,i);
            }
            if(!end && generals[1-side].hp>0) seed(generals[1-side].x,battle_commander_y,401-side);
            // Multi-source distance field: all ranks can locate the remaining
            // enemy ranks without scanning all 200 opponents for every actor.
            for(int begin=0;begin<end;++begin) {
                const int current=queue[begin], column=current%columns;
                const int adjacent[4]={column ? current-1 : -1,column+1<columns ? current+1 : -1,
                                       current-columns,current+columns};
                for(int location:adjacent) if(location>=0 && location<cell_count && field[location]<0) {
                    field[location]=field[current]; queue[end++]=int16_t(location);
                }
            }
            pursuit_ready[side]=true;
        }
        const auto& actor=soldiers[index];
        const int x=actor.move_ticks ? actor.move_target_x : actor.x;
        const int y=actor.move_ticks ? actor.move_target_y : actor.y;
        const int location=cell(x,y);
        const int cached=location>=0 ? pursuit[side][location] : -1;
        if(cached>=0 && cached<400 && soldiers[cached].hp>0) return cached;
        if(cached>=400 && generals[cached-400].hp>0) return cached;
        // A target can die after the field was built earlier this update.
        // Immediately pick another survivor, or the commander if none remain.
        int target=-1, distance=0x7fffffff;
        for(int i=(1-side)*200;i<(2-side)*200;++i) if(soldiers[i].hp>0) {
            const auto& candidate=soldiers[i];
            const int dx=x-(candidate.move_ticks ? candidate.move_target_x : candidate.x);
            const int dy=y-(candidate.move_ticks ? candidate.move_target_y : candidate.y);
            const int score=(dx<0 ? -dx : dx)*3+(dy<0 ? -dy : dy)*4;
            if(score<distance) { target=i; distance=score; }
        }
        return target>=0 ? target : generals[1-side].hp>0 ? 401-side : -1;
    }
    SANGO_MOVEMENT_HOT bool pursue(Soldier& actor,int target,Fighter* generals,Game& game) {
        if(target<0) return false;
        const int target_x=target<400 ? (soldiers[target].move_ticks ? soldiers[target].move_target_x : soldiers[target].x) : generals[target-400].x;
        const int target_y=target<400 ? (soldiers[target].move_ticks ? soldiers[target].move_target_y : soldiers[target].y) : battle_commander_y;
        static constexpr int8_t dx[8]={1,1,0,-1,-1,-1,0,1};
        static constexpr int8_t dy[8]={0,-1,-1,-1,0,1,1,1};
        int best=-1, best_score=0x7fffffff;
        const int start=game.roll(8);
        for(int i=0;i<8;++i) {
            const int direction=(start+i)&7;
            const int x=actor.x+dx[direction]*battle_grid_column_width;
            const int y=actor.y+dy[direction]*battle_grid_row_height;
            if(x<0 || x>=battlefield_width || y<0 || y>battle_grid_max_y || occupied(x,y)) continue;
            bool general_occupied=false;
            for(int side=0;side<2;++side)
                if(generals[side].hp>0 && cell(generals[side].x,battle_commander_y)==cell(x,y)) general_occupied=true;
            if(general_occupied) continue;
            const int distance_x=target_x-x, distance_y=target_y-y;
            // Approach an attack position in the target's row, rather than
            // piling vertically against a unit that can only be hit sideways.
            const int score=(distance_x<0 ? -distance_x : distance_x)*3+
                            (distance_y<0 ? -distance_y : distance_y)*4+(distance_y ? 60 : 0);
            if(score<best_score) { best=direction; best_score=score; }
        }
        if(best<0) return false;
        actor.move_target_x=int16_t(actor.x+dx[best]*battle_grid_column_width);
        actor.move_target_y=int16_t(actor.y+dy[best]*battle_grid_row_height);
        actor.move_dx=dx[best]; actor.move_dy=dy[best];
        actor.move_x_remainder=actor.move_y_remainder=0;
        actor.move_ticks=32;
        relocate(&actor-soldiers,actor.move_target_x,actor.move_target_y);
        return true;
    }
    [[gnu::noinline]] void relocate(int index,int x,int y) {
        const int destination=cell(x,y);
        if(cells[index]==destination) return;
        if(cells[index]>=0) {
            auto* link=&heads[cells[index]];
            while(*link>=0 && *link!=index) link=&next[*link];
            if(*link==index) *link=next[index];
        }
        cells[index]=int16_t(destination); next[index]=-1;
        if(destination>=0) { next[index]=heads[destination]; heads[destination]=int16_t(index); }
    }
};
#if defined(__arm__)
__attribute__((section(".ewram")))
#endif
MovementOccupancy movement_occupancy;
}

[[gnu::noinline]] int demake_soldier_type(int native_force);
[[gnu::noinline]] int demake_soldier_block_rate(int attack_type,int target_type);
[[gnu::noinline]] bool demake_soldier_projectile_hit(Game& game,int attacker_side,int target_index);
[[gnu::noinline]] bool demake_soldier_damage_general(Game& game,int attacker_side);

// This three-object area scheduler runs only ten region checks per cast.
// Keep it in ROM, leaving scarce IWRAM for pixel blending and unit movement.
[[gnu::noinline]]
#if defined(__arm__)
__attribute__((section(".text")))
#endif
void Game::update_fire_dragon_projectile(Projectile& p) {
    const int age=++p.height;
    const int direction=p.dx>0 ? 1 : -1;
    p.x=int16_t(p.target_offset_x-direction*100+direction*minimum(age,60)*10/3);
    if(p.owner_index==0 && age<=55 && (age-1)%6==0) {
        const int step=(age-1)/6;
        const int start=p.target_offset_x+direction*(-100+step*20);
        const int left=direction>0 ? start : start-20;
        const int right=left+20;
        const int radius=int(p.scatter)*30;
        auto& force=battle.sides[p.target_side];
        for(int index=0;index<200;++index) {
            auto& soldier=battle.soldiers[int(p.target_side)*200+index];
            if(soldier.hp<=0 || soldier.x<left || soldier.x>right ||
               soldier.y<battle_commander_y-radius || soldier.y>battle_commander_y+radius)
                continue;
            soldier.hp=0;
            --force.soldiers;
            if(!soldier.rear) --force.front;
        }
        // demake's burning attachment rejects repeated fire hits.
        if(p.collision_enabled && force.hp>0 && force.x>=left && force.x<=right) {
            apply_magic_general_callback(p.source_script,p.target_side,0);
            p.collision_enabled=0;
        }
    }
    if(age>=90) p.active=0;
}

// Keep this skill-specific walk in ROM, not the nearly full IWRAM hot path.
[[gnu::noinline]] void Game::hit_ghost_halberd_cell(Projectile& p,int head,const int16_t* cell_next,int column,int row) {
    for(int index=head;index>=0;index=cell_next[index]) {
        auto& soldier=battle.soldiers[index];
        if(soldier.hp<=0 || soldier.side!=p.target_side) continue;
        soldier.hp=0;
        auto& force=battle.sides[p.target_side];
        --force.soldiers;
        if(!soldier.rear) --force.front;
    }
    // 1 = soldier + general collision; 2 = soldiers only after one general
    // callback. Keep the same phase so rendering and camera tracking continue.
    if(p.collision_enabled!=1) return;
    constexpr int origin=battlefield_width/2-28*battle_grid_column_width;
    const auto& general=battle.sides[p.target_side];
    const int offset=int(general.x)-origin+battle_grid_column_width/2;
    if(general.hp>0 && offset>=0 && offset/battle_grid_column_width==column &&
       (battle_commander_y+battle_grid_row_height/2)/battle_grid_row_height==row) {
        apply_magic_general_callback(p.source_script,p.target_side,1);
        p.collision_enabled=2;
    }
}

#if defined(__arm__)
__attribute__((section(".iwram")))
#endif
void Game::update_fast_projectile(Projectile& p,const int16_t* cell_heads,const int16_t* cell_next) {
    constexpr int grid_origin_x=battlefield_width/2-28*battle_grid_column_width;
    constexpr int indexed_columns=battle_grid_columns+1;
    auto grid_column=[](int x) {
        const int offset=x-grid_origin_x+battle_grid_column_width/2;
        return offset>=0 ? offset/battle_grid_column_width : -1;
    };
    auto grid_row=[](int y) {
        const int offset=y+battle_grid_row_height/2;
        return offset>=0 ? offset/battle_grid_row_height : -1;
    };
    auto cell_head=[&](int column,int row) {
        return unsigned(column)<indexed_columns && unsigned(row)<battle_grid_rows ?
            int(cell_heads[row*indexed_columns+column]) : -1;
    };
    auto note_miss = [&]() {
        // Native miss/friendly event randomizes the heading, vertical arc and
        // a short scheduling delay, then runs a 24-update non-collision phase.
        // This coordinate model has no vertical arc, and flipping dx made a
        // missed arrow visibly fly back at its owner. Keep the heading and let
        // it pass through and expire; the RNG draws are preserved so the
        // battle's random stream is unchanged.
        (void)roll(96);
        (void)roll(3);
        (void)roll(0x40000);
        (void)roll(6);
    };
    if(p.magic==1 && p.phase==0) {
        update_magic_projectile_windup(p);
        return;
    }
    if(p.magic==1 && p.source_script>=2642 && p.source_script<=2644) {
        // demake FireDragon.ShowDragon emits ten 20-wide strips at .2s
        // intervals. One scheduler handles the whole region; the other
        // lanes are visual bodies, not additional general damage callbacks.
        update_fire_dragon_projectile(p);
        return;
    }
        if(p.magic == 1) {
            // GLOBAL 4100/4200 and MAGIC01 4500 configure a horizontal
            // 16-pixel/update path through 96-pixel cells.
            int remainder, divisor;
            if(p.kind == 1 || (p.source_script>=2645 && p.source_script<=2647)) {
                // GLOBAL 6202 and GLOBAL 4600 ramp native speed from 1 to 24
                // pixels/update. Both convert through the 40/96 battle-world
                // scale with a 12ths remainder; their increment counters are
                // stored separately because clone owner_index identifies a
                // potentially excluded soldier slot during collision checks.
                const int native_speed = int(p.target_offset_x) / 16;
                divisor = 12;
                remainder = int(p.movement_remainder) +
                    (p.dx > 0 ? native_speed*5 : -native_speed*5);
                if(p.target_offset_x < 24*16) {
                    const int increment = p.kind == 1 ? int(p.owner_index) : int(p.scatter);
                    p.target_offset_x = int16_t(p.target_offset_x + increment);
                    if(p.kind == 1) ++p.owner_index;
                    else ++p.scatter;
                }
            } else {
                remainder = int(p.movement_remainder) + (p.dx > 0 ? 20 : -20);
                divisor = 3;
            }
            const int step = remainder / divisor;
            p.movement_remainder = int8_t(remainder - step * divisor);
            p.x = int16_t(p.x + step);
            if(((p.source_script>=2633 && p.source_script<=2635) ||
                p.source_script==2660) && p.height<32767)
                ++p.height; // M012 companion sequences use #2/#4 frame clocks.
            // GLOBAL4100 creates its animated sequence4100 at launch; its
            // M011WaveA frames advance every two object updates from age0.
            if(p.source_script>=2630 && p.source_script<=2632 && p.height<32767)
                ++p.height;
            if(p.source_script>=2642 && p.source_script<=2644) {
                // MAGIC01 4501 draws RNG(0,20) every loop; on zero it uses
                // native27 to change the core's collision extents, not its
                // position. The source cell lookup uses only x/y, so consume
                // the branch draw without adding invented visual jitter.
                (void)roll(21);
                if(p.height<32767) ++p.height;
            }
        } else {
            p.x = int16_t(p.x + p.dx);
        }
        if(p.x < 0 || p.x >= battlefield_width) { p.active = 0; return; }
        if(!p.magic) {
            int traveled=int(p.x)-p.target_offset_x;
            if(traveled<0) traveled=-traveled;
            if(traveled*2200>1000*battlefield_width) {p.active=0;return;}
        }
        if(p.magic == 1 && (p.x < grid_origin_x - battle_grid_column_width/2 ||
                            p.x >= grid_origin_x + battle_grid_columns*battle_grid_column_width + battle_grid_column_width/2)) {
            p.active = 0;
            return;
        }
        if(p.remaining) {
            if(!--p.remaining) p.active = 0;
            return;
        }
        if(!p.collision_enabled) return;
        // Deployment maps native columns to 40-unit cell centers and rows to
        // 30-unit centers. Round moving actors to the occupied grid cell as
        // the native projectile update does after converting its position.
        const int projectile_column = grid_column(p.x);
        const int projectile_row = grid_row(p.y);
        if(projectile_column >= 0 && projectile_column < 56 && projectile_row >= 0 && projectile_row < 14) {
            if(p.magic == 1) {
                if(p.source_script==2635) {
                    hit_ghost_halberd_cell(p,cell_head(projectile_column,projectile_row),cell_next,
                                           projectile_column,projectile_row);
                    return;
                }
                for(int index=cell_head(projectile_column,projectile_row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                    if(target.hp <= 0) continue;
                    // The crescent slash and the ghost halberd cut down soldiers
                    // in the cells they sweep; the bow shots pass through them.
                    if(target.side == p.target_side && original::magic_projectile_cuts_soldiers(p.source_script)) {
                        auto& force = battle.sides[target.side];
                        target.hp = 0;
                        --force.soldiers;
                        if(!target.rear) --force.front;
                    }
                    break;
                }
                // These magic shots pass through soldier cells; occupation
                // must not suppress the independent commander callback.
                {
                    auto& target = battle.sides[p.target_side];
                    if(target.hp > 0 && grid_column(target.x) == projectile_column &&
                       grid_row(battle_commander_y) == projectile_row) {
                        // These effects never link to a battle unit (+0x80
                        // stays null), so FUN_0043f80f returns zero. Their
                        // configured callback supplies the fixed damage.
                        const int next_callback_base = original::magic_general_callback_next_base(p.source_script);
                        const bool shared_callback = next_callback_base &&
                            (p.source_script != 2664 || p.kind == 1);
                        const bool level_scaled_callback = p.source_script >= 2642 && p.source_script <= 2644;
                        const uint8_t callback_mode = level_scaled_callback ? 2 : shared_callback ? 1 : 0;
                        apply_magic_general_callback(p.source_script,p.target_side,callback_mode);
                        // The effect ended on the hit: stop colliding and expire
                        // on the next update instead of flying on past the
                        // general as if it had missed.
                        p.collision_enabled = 0;
                        p.remaining = 1;
                    }
                }
                return;
            }
            bool projectile_hit_soldier=false;
            bool enemy_cell_occupied=false;
            for(int index=cell_head(projectile_column,projectile_row); index>=0; index=cell_next[index]) {
                auto& target = battle.soldiers[index];
                if(target.hp <= 0) continue;
                if(target.side == 1 - p.target_side && target.index == p.owner_index) continue;
                // demake's ranged projectile collides only with enemy cells;
                // allied soldiers do not absorb a shot passing through ranks.
                if(target.side != p.target_side) continue;
                enemy_cell_occupied=true;
                projectile_hit_soldier=demake_soldier_projectile_hit(*this,1-p.target_side,index) ||
                                       projectile_hit_soldier;
            }
            if(enemy_cell_occupied) {
                p.collision_enabled = 0;
                p.remaining = projectile_hit_soldier ? 8 : 24;
                if(!projectile_hit_soldier) note_miss();
            }
            auto& target=battle.sides[p.target_side];
            if(target.hp>0 && grid_column(target.x)==projectile_column &&
               grid_row(battle_commander_y)==projectile_row) {
                const bool hit=demake_soldier_damage_general(*this,1-p.target_side);
                p.collision_enabled = 0;
                p.remaining = hit ? 8 : 24;
                if(!hit) note_miss();
            }
        }
}

void Game::update_firewheel_projectile(Projectile& p,const int16_t* cell_heads,const int16_t* cell_next) {
    if(p.phase==0) {
        if(p.remaining) --p.remaining;
        if(!p.remaining) {
            p.phase=1;
            p.owner_index=uint8_t(original::fire_wheel_active_updates);
        }
        return;
    }
    if(p.kind==3 && p.owner_index<original::fire_wheel_active_updates)
        p.scatter=uint8_t(p.scatter+3);
    // GLOBAL 2501/2502 increment local3 each update and pass local3*16 as
    // native21's radius. The collider therefore expands from radius16 to256.
    const int radius_step=original::fire_wheel_active_updates-int(p.owner_index)+1;
    const int native_radius=radius_step*16;
    const int native_x=magic_native_polar_offset(native_radius,p.scatter+64);
    const int native_y=magic_native_polar_offset(native_radius,p.scatter);
    // demake MagicBaMianHuo captures posGeneral once in Init. Moving the
    // commander afterward must not drag either the fire or its collision path.
    const int center_x=p.target_offset_x;
    const int center_y=p.height;
    p.x=int16_t(center_x+native_x*battle_grid_column_width/96);
    p.y=int16_t(center_y+native_y*battle_grid_row_height/72);

    const int column_offset=int(p.x)+battle_grid_column_width/2;
    const int row_offset=int(p.y)+battle_grid_row_height/2;
    const int column=column_offset>=0 ? column_offset/battle_grid_column_width : -1;
    const int row=row_offset>=0 ? row_offset/battle_grid_row_height : -1;
    bool soldier_occupies_cell=false;
    if(column>=0 && column<battle_grid_columns && row>=0 && row<battle_grid_rows) {
        const int head=cell_heads[row*(battle_grid_columns+1)+column];
        for(int index=head; index>=0; index=cell_next[index]) {
            auto& target=battle.soldiers[index];
            if(target.hp<=0) continue;
            soldier_occupies_cell=true;
            if(target.side==p.target_side) {
                auto& force=battle.sides[target.side];
                target.hp=0;
                --force.soldiers;
                if(!target.rear) --force.front;
            }
            break; // Native cell lookup returns one occupied unit.
        }
        if(!soldier_occupies_cell) {
            auto& target=battle.sides[p.target_side];
            if(target.hp>0 && (int(target.x)+battle_grid_column_width/2)/battle_grid_column_width==column &&
               battle_commander_row==row)
                apply_magic_general_callback(p.source_script,p.target_side,1);
        }
    }
    if(p.owner_index && --p.owner_index==0) p.active=0;
}

void Game::update_battle_projectiles(const int16_t* cell_heads,const int16_t* cell_next) {
    constexpr int grid_origin_x=battlefield_width/2-28*battle_grid_column_width;
    constexpr int indexed_columns=battle_grid_columns+1;
    auto grid_column=[](int x) {
        const int offset=x-grid_origin_x+battle_grid_column_width/2;
        return offset>=0 ? offset/battle_grid_column_width : -1;
    };
    auto grid_row=[](int y) {
        const int offset=y+battle_grid_row_height/2;
        return offset>=0 ? offset/battle_grid_row_height : -1;
    };
    auto cell_head=[&](int column,int row) {
        return unsigned(column)<indexed_columns && unsigned(row)<battle_grid_rows ?
            int(cell_heads[row*indexed_columns+column]) : -1;
    };
    auto spawn_3103_hit_visual=[&](int source_script,int target_side,int soldier_index) {
        if((source_script!=2622 && source_script!=2623) ||
           target_side<0 || target_side>1) return;
        for(auto& hit : battle.projectiles) if(!hit.active) {
            hit={};
            hit.active=1;
            hit.magic=17;
            hit.remaining=76; // GLOBAL2504's 60-update follow + native38(16,1).
            hit.source_script=int16_t(source_script);
            hit.target_side=int8_t(target_side);
            hit.owner_index=soldier_index<0 ? 0xff : uint8_t(soldier_index%200);
            break;
        }
    };
    auto spawn_3904_particles=[&](int source_script,int x,int y,int count) {
        for(int particle_index=0;particle_index<count;++particle_index) {
            Projectile* particle=nullptr;
            for(auto& candidate : battle.projectiles)
                if(!candidate.active) { particle=&candidate; break; }
            if(!particle) break;
            // MAGIC01 3904 passes one RNG(0,255) angle to native17 as the
            // object's orientation; native23 uses that stored angle with the
            // following horizontal RNG(4,12) and vertical RNG(-2,8).
            const int angle=roll(256);
            const int speed=4+roll(9);
            const int vertical_speed=-2+roll(11);
            *particle={};
            particle->active=1;
            particle->magic=21;
            particle->phase=1;
            particle->owner_index=uint8_t(battle.tick); // creation update.
            particle->source_script=int16_t(source_script);
            particle->x=int16_t(x);
            particle->y=int16_t(y);
            particle->height=0;
            particle->remaining=32;
            particle->dx=int16_t(magic_planar_particle_velocity(speed,angle,0));
            particle->target_offset_x=int16_t(magic_planar_particle_velocity(speed,angle,1));
            particle->attacker_rear=uint8_t(int8_t(
                vertical_speed*battle_grid_row_height*8/72));
            particle->scatter=128;
        }
    };
    // Native 0x43F098 updates projectile positions, maps the new position to
    // a battle-grid cell, and checks whichever unit occupies that cell. Keep
    // a small fixed pool for GBA instead of applying ranged attacks at launch.
    for(auto& p : battle.projectiles) if(p.active) {
        if(p.magic==37) continue; // Reserved for a future composite child.
        if(p.magic==34) {
            if(p.owner_index==uint8_t(battle.tick)) continue;
            if(p.remaining && --p.remaining) continue;
            p.active=0;
            if(battle.effect_script==p.source_script && battle.effect_side==p.target_side)
                continue; // The ordinary global windup already launched it.
            const auto script=battle.effect_script;
            const auto side=battle.effect_side,applied=battle.effect_applied;
            const auto state=battle.magic_field_state;
            const auto center=battle.magic_field_center_x;
            battle.effect_script=p.source_script;
            battle.effect_side=uint8_t(p.target_side);
            battle.effect_applied=0;
            battle.magic_field_state=0;
            apply_pending_effect();
            battle.effect_script=script; battle.effect_side=side;
            battle.effect_applied=applied; battle.magic_field_state=state;
            battle.magic_field_center_x=center;
            continue;
        }
        if(p.magic==32 || p.magic==33) {
            // MAGIC01 3712 creates ten independent shards plus one central
            // M007Break2 object. Each shard carries its own native23 vector
            // and native38 lifetime; the central sequence holds for #9999.
            if(p.magic==32 && p.height<40) {
                const int age=int(p.height)+1;
                p.height=int16_t(age);
                // MAGIC01 3713 initially inherits the enemy-force mask from
                // global22, then native43 clears 0x05000000 once the local
                // lifetime reaches25. The shards can therefore kill enemy
                // soldiers during their short initial collision window, but
                // never hit either general.
                if(p.remaining>25) {
                    const int offset_x=int(p.dx)*age/8;
                    const int offset_y=int(p.target_offset_x)*age/8;
                    const int z_eighth=magic_native_gravity_height_eighth(
                        int8_t(p.attacker_rear),age);
                    const int z_world=maximum(0,z_eighth/8);
                    if(z_world<battle_grid_row_height*96/72) {
                        const int column=grid_column(int(p.x)+offset_x);
                        const int row=grid_row(int(p.y)+offset_y);
                        if(column>=0 && column<battle_grid_columns &&
                           row>=0 && row<battle_grid_rows) {
                            // FUN_0043c227 returns one process for the cell.
                            // A matching enemy soldier receives event0x10003
                            // and is removed immediately; the non-bomb shard
                            // continues along its arc.
                            for(int index=cell_head(column,row); index>=0;
                                index=cell_next[index]) {
                                auto& target=battle.soldiers[index];
                                if(target.hp<=0) continue;
                                if(target.side==p.target_side) {
                                    auto& force=battle.sides[int(target.side)];
                                    target.hp=0;
                                    --force.soldiers;
                                    if(!target.rear) --force.front;
                                    // Fire-stone callbacks start MAGIC01
                                    // 3103's hit animation on the struck unit.
                                    spawn_3103_hit_visual(p.source_script,
                                        int(p.target_side),index%200);
                                }
                                break;
                            }
                        }
                    }
                }
            } else if(p.magic==33 && p.height<40) {
                ++p.height;
            }
            if(p.remaining && !--p.remaining) p.active=0;
            continue;
        }
        if(p.magic==24) {
            // Deferred MAGIC01 3904 child-script group. Skip the update in
            // which the callback creates this token, then honor its WAIT8/16
            // before drawing RNG and allocating that group's 60 particles.
            if(p.kind) { p.kind=0; continue; }
            if(p.remaining && --p.remaining) continue;
            const int source_script=p.source_script;
            const int x=p.x, y=p.y;
            p.active=0;
            spawn_3904_particles(source_script,x,y,60);
            continue;
        }
        if(p.magic==22) {
            if(p.height<32767) ++p.height;
            if(p.remaining && !--p.remaining) p.active=0;
            continue;
        }
        if(p.magic==35) {
            if(p.remaining && --p.remaining) continue;
            const int x=p.x,y=p.y;
            p.active=0;
            spawn_m014_sparks(x,y);
            continue;
        }
        if(p.magic==1 && p.kind==1 && p.phase==0) {
            if(p.remaining) --p.remaining;
            if(!p.remaining) {
                // GLOBAL 6202 is launched after GLOBAL 6200's 110-update
                // setup, then draws RNG(0,3) and, on one branch, RNG(0,90)
                // before its 15+10 update object-creation delay.
                int optional_delay=0;
                if(roll(4)==3) optional_delay=roll(91);
                p.phase=3;
                p.remaining=uint8_t(25+optional_delay);
            }
            continue;
        }
        if(p.magic==1 && p.kind==1 && p.phase==3) {
            if(p.remaining) --p.remaining;
            if(!p.remaining) p.phase=1;
            continue;
        }
        const bool arrow_visual_script=p.source_script>=2600 && p.source_script<=2605;
        if((p.magic==18 || p.magic==19 || p.magic==26) &&
           (p.source_script==2664 || arrow_visual_script)) {
            if(p.magic==19 && arrow_visual_script) {
                if(p.phase==0) {
                    if(p.remaining) --p.remaining;
                    if(!p.remaining) {
                        p.phase=1;
                        // 3101/3102 first wait the sampled child delay, then
                        // hold the initial Fire3 position for38 updates.
                        p.remaining=uint8_t(int(p.target_offset_x)+38);
                    }
                    continue;
                }
                const int side_sign=p.target_side==0 ? 1 : -1;
                if(p.phase==1) {
                    if(p.remaining && !--p.remaining) {
                        p.x=int16_t(clamp(int(p.x)+side_sign*14*battle_grid_column_width/96,
                                          0,battlefield_width-1));
                        p.height=int16_t(p.height+31*battle_grid_row_height/72);
                        p.phase=2;
                        p.remaining=8;
                    }
                    continue;
                }
                if(p.phase==2) {
                    if(p.remaining && !--p.remaining) {
                        p.x=int16_t(clamp(int(p.x)-side_sign*10*battle_grid_column_width/96,
                                          0,battlefield_width-1));
                        p.height=int16_t(p.height+30*battle_grid_row_height/72);
                        p.phase=3;
                        p.remaining=8;
                    }
                    continue;
                }
                if(p.phase==3 && p.remaining && !--p.remaining) p.active=0;
                continue;
            }
            if(p.phase==0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    p.height=0;
                    if(p.magic==26 && p.source_script==2664) {
                        // Sequence3000/3001 is a separate child object. It
                        // starts after its per-cell RNG(1,30) wait.
                        p.phase=3;
                        p.remaining=uint8_t(1+roll(30));
                    } else {
                        p.phase=1;
                        p.remaining=p.magic==19 ? 128 : 10;
                    }
                }
                continue;
            }
            if(p.magic==26 && p.phase==3) {
                if(p.remaining && !--p.remaining) {
                    p.phase=1;
                    p.height=0;
                }
                continue;
            }
            ++p.height;
            const int lifetime=p.magic==26 ? 116 : int(p.remaining);
            if(p.height>=lifetime) p.active=0;
            continue;
        }
        if(p.magic==31 && arrow_visual_script) {
            if(p.phase==0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    p.phase=1;
                    p.height=0;
                    p.remaining=80;
                    p.movement_remainder=0;
                    p.height_remainder=0;
                }
                continue;
            }
            // Sequence3002 is a non-colliding projectile. MAGIC01 3001 sets
            // native24 azimuth128/elevation32/speed32; use the same fixed
            // point velocity scale as the matching arrow family and rise
            // instead of descending. Its Things.ini sequence lifetime is80.
            int x_remainder=int(p.movement_remainder)+(p.dx<0 ? -226 : 226);
            const int x_step=x_remainder/24;
            p.movement_remainder=int8_t(x_remainder-x_step*24);
            p.x=int16_t(clamp(int(p.x)+x_step,0,battlefield_width-1));
            int z_remainder=int(p.height_remainder)+226;
            const int z_step=z_remainder/24;
            p.height_remainder=int8_t(z_remainder-z_step*24);
            p.height=int16_t(p.height+z_step);
            if(p.remaining && !--p.remaining) p.active=0;
            continue;
        }
        if(p.magic==15 && (p.source_script==2618 || p.source_script==2622 || p.source_script==2623)) {
            if(p.phase==2) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    if(p.kind&2) {
                        p.phase=3; // separate parent sequence3701 marker
                        p.remaining=10;
                    } else {
                        // Each MAGIC01 3701 child gets its own RNG(1,30)
                        // delay before creating sequence3700.
                        p.phase=4;
                        p.remaining=uint8_t(1+roll(30));
                    }
                }
                continue;
            }
            if(p.phase==3) {
                if(p.remaining && !--p.remaining) p.active=0;
                continue;
            }
            if(p.phase==4) {
                if(p.remaining && !--p.remaining) {
                    p.phase=1;
                    p.height=0;
                }
                continue;
            }
            if(p.phase==1) {
                ++p.height; // Sequence3700 remains alive for113 updates.
                if(p.height>=113) p.active=0;
            }
            continue;
        }
        if(p.magic==20 && (p.source_script==2627 || p.source_script==2628)) {
            if(p.phase==2) continue; // reserved until MAGIC01 4000's 65-update setup.
            if(p.phase==0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    p.phase=1;
                    p.height=0;
                }
                continue;
            }
            // native24 supplies an 8-native-pixel horizontal velocity;
            // preserve its 40/96 world conversion with a remainder.
            int x_remainder=int(p.movement_remainder)+int(p.dx)*battle_grid_column_width;
            const int x_step=x_remainder/96;
            p.movement_remainder=int8_t(x_remainder-x_step*96);
            p.x=int16_t(int(p.x)+x_step);
            p.y=int16_t(int(p.y)+p.target_offset_x);
            const int column=grid_column(p.x);
            const int row=grid_row(p.y);
            if(column>=0 && column<battle_grid_columns && row>=0 && row<battle_grid_rows) {
                for(int index=cell_head(column,row);index>=0;index=cell_next[index]) {
                    auto& target=battle.soldiers[index];
                    if(target.hp<=0 || target.side==p.target_side) continue;
                    // GLOBAL4000/2508 set mask0x02000000. FUN_0040b01c sends
                    // soldier contact event0x10003, which removes the unit.
                    auto& force=battle.sides[target.side];
                    target.hp=0;
                    --force.soldiers;
                    if(!target.rear) --force.front;
                    // Chariots only run over soldiers. Do not attach the
                    // generic 3904 light burst: repeated contacts fill the
                    // particle pool and turn a moving cart into explosions.
                    break;
                }
            }
            if(p.height<32767) ++p.height;
            if(p.height>=316) p.active=0; // GLOBAL2508 WAIT300 + native38(16,1).
            continue;
        }
        if(p.magic==15 && p.source_script==2664) {
            if(p.phase==2) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    if(p.owner_index==0) {
                        p.phase=1;
                        p.height=0;
                        for(auto& child : battle.projectiles) if(child.active && child.magic==15 &&
                            child.source_script==2664 && child.phase==2 && child.owner_index>0) {
                            child.phase=3;
                            child.remaining=uint8_t(1+roll(30));
                        }
                    } else {
                        p.phase=3;
                        p.remaining=uint8_t(1+roll(30));
                    }
                }
                continue;
            }
            if(p.phase==3) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) { p.phase=1; p.height=0; }
                continue;
            }
            if(p.phase==1) {
                ++p.height;
                if(p.owner_index==0 ? p.height>=10 : p.height>=113) p.active=0;
            }
            continue;
        }
        if(p.magic==13 || p.magic==14) {
            if(p.phase==2) continue;
            if(p.magic==13 && p.source_script==2665) {
                if(p.phase==0 && p.remaining) --p.remaining;
                if(p.phase==0 && !p.remaining) {
                    p.phase=1;
                    p.remaining=16; // Things.ini sequences6200-6203 use native38(16,1).
                } else if(p.phase==1 && p.remaining && !--p.remaining) {
                    p.active=0;
                }
                continue;
            }
            if(p.magic==14) {
                if(p.phase==0) {
                    // GLOBAL6001/6002 draw one of the six M020ST frames,
                    // azimuth 0..256, then elevation 32..96 before native24
                    // assigns a six- or seven-unit three-axis velocity. native38's
                    // 32/1 timer removes this non-colliding visual object;
                    // native37 separately shrinks its extents by 512/update.
                    p.kind=uint8_t(roll(6));
                    const int azimuth=roll(257);
                    const int elevation=32+roll(65);
                    const int speed=p.owner_index==7 ? 7 : 6;
                    p.dx=int16_t(magic_particle_velocity(speed,azimuth,elevation,0));
                    p.target_offset_x=int16_t(magic_particle_velocity(speed,azimuth,elevation,1));
                    p.attacker_rear=uint8_t(elevation);
                    p.movement_remainder=0;
                    p.scatter=128;
                    p.height=0;
                    p.remaining=32;
                    p.phase=1;
                    continue;
                }
                int x_remainder=int(p.movement_remainder)+p.dx;
                const int x_step=x_remainder/8;
                p.movement_remainder=int8_t(x_remainder-x_step*8);
                int y_remainder=int(p.scatter)-128+p.target_offset_x;
                const int y_step=y_remainder/8;
                p.scatter=uint8_t(y_remainder-y_step*8+128);
                p.x=int16_t(int(p.x)+x_step);
                p.y=int16_t(int(p.y)+y_step);
                ++p.height; // age; native29 applies gravity after each position update.
                if(p.remaining) --p.remaining;
                if(!p.remaining) p.active=0;
                continue;
            }
            ++p.height;
            // GLOBAL 2511 leaves the M020swa/swb object alive for its local
            // wait8 plus native38(16,1) timer, then the object updater deletes
            // it. The swc sequence removes itself sooner through NULSHAPE.
            const int lifetime=p.kind==0 ? 24 :
                (p.source_script==2659 ? 7 : 5);
            if(p.height>=lifetime) p.active=0;
            continue;
        }
        if(p.magic==21 && p.owner_index==uint8_t(battle.tick)) {
            // MAGIC01 3904 creates these during another object's update.
            // Preserve the creation frame; move on the next battle update.
            continue;
        }
        if(p.magic==21 && p.phase==1) {
            int x_remainder=int(p.movement_remainder)+p.dx;
            const int x_step=x_remainder/8;
            p.movement_remainder=int8_t(x_remainder-x_step*8);
            const int y_velocity=int(p.target_offset_x);
            int y_remainder=int(p.scatter)-128+y_velocity;
            const int y_step=y_remainder/8;
            p.scatter=uint8_t(y_remainder-y_step*8+128);
            int z_remainder=int(p.height_remainder)+int8_t(p.attacker_rear);
            const int z_step=z_remainder/8;
            p.height_remainder=int8_t(z_remainder-z_step*8);
            p.x=int16_t(p.x+x_step);
            p.y=int16_t(p.y+y_step);
            // M08B has OF_NOGRAVITY: native23's vertical component stays
            // constant until the object processor clamps a below-ground z.
            const int next_height=int(p.height)+z_step;
            if(next_height<0) {
                p.height=0;
                p.attacker_rear=0;
                p.height_remainder=0;
            } else p.height=int16_t(next_height);
            if(p.remaining) --p.remaining;
            if(!p.remaining) p.active=0;
            continue;
        }
        if(p.magic==12) {
            if(p.source_script==2650 && p.phase==0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) p.phase=1;
                continue;
            }
            if(p.height>0) --p.height;
            if(!p.height) p.active=0;
            continue;
        }
        if(p.magic==27) {
            if(p.phase==0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) p.phase=1;
                continue;
            }
            // Sequences4101 are independent static children, not part of the
            // moving sequence4100 collision object. Their #9999 hold lasts
            // for the remainder of this battle.
            if(p.height<32767) ++p.height;
            continue;
        }
        if(p.magic<=1) {
            if(p.magic==1 && p.kind>=2) update_firewheel_projectile(p,cell_heads,cell_next);
            else update_fast_projectile(p,cell_heads,cell_next);
            continue;
        }
        if(p.magic==36) {
            // Invisible rear-entry camera token. No collision or damage.
            if(p.remaining) --p.remaining;
            if(!p.remaining) {
                if(!p.phase) {p.phase=1;p.remaining=30;}
                else p.active=0;
            }
            continue;
        }
        if(p.magic==25) {
            if(p.phase==0) {
                // Child3602's fire plumes begin at their individually queued
                // script tick. Store their absolute cast age because the
                // source's parent/sibling RNG waits can push the last plume
                // past an 8-bit relative-delay range.
                if(p.target_offset_x>0 && --p.target_offset_x>0) continue;
                p.phase=1;
                p.remaining=60; // Things.ini sequences3608-3610 use WAIT=#60.
                if(p.source_script==2617) {
                    // demake FireDown calls OnMagicHitChecking with an
                    // 80x80 region and isFire=true: every matching soldier
                    // dies without a block roll, while a matching general
                    // takes the fixed sequence-14 damage once.
                    const int target_side=int(p.target_side);
                    for(int index=0;index<200;++index) {
                        auto& target=battle.soldiers[target_side*200+index];
                        int dx=int(target.x)-int(p.x);
                        int dy=int(target.y)-int(p.y);
                        if(target.hp<=0 || dx < -40 || dx > 40 || dy < -40 || dy > 40)
                            continue;
                        target.hp=0;
                        auto& force=battle.sides[target_side];
                        --force.soldiers;
                        if(!target.rear) --force.front;
                    }
                    auto& general=battle.sides[target_side];
                    int general_dx=int(general.x)-int(p.x);
                    int general_dy=battle_commander_y-int(p.y);
                    if(p.collision_enabled && general.hp>0 &&
                       general_dx>=-40 && general_dx<=40 &&
                       general_dy>=-40 && general_dy<=40) {
                        apply_magic_general_callback(p.source_script,target_side,0);
                        for(auto& fire:battle.projectiles) if(fire.active && fire.magic==25 &&
                            fire.source_script==2617 && fire.target_side==target_side)
                            fire.collision_enabled=0; // demake hitGeneral latch survives child changes.
                    }
                }
                continue;
            }
            int height_remainder=int(p.height_remainder)+int(p.dx)*battle_grid_row_height;
            const int height_step=height_remainder/72;
            p.height_remainder=int8_t(height_remainder-height_step*72);
            p.height=int16_t(int(p.height)+height_step);
            if(p.remaining) --p.remaining;
            if(!p.remaining) p.active=0;
            continue;
        }
        if(p.magic==23) {
            if(p.phase==0) {
                // Sequence3620 objects are created at update215 plus their
                // own 4..10 update delay. Their #9999 E074-1 frame has no
                // script timer; absent a collision/removal they persist for
                // the remainder of the battle.
                if(p.target_offset_x>0 && --p.target_offset_x>0) continue;
                if(p.remaining) {
                    --p.remaining;
                    continue;
                }
                p.phase=1;
            }
            continue;
        }
        if(p.magic == 9) {
            if(p.target_side < 0 || p.target_side > 1) {
                p.active = 0;
                continue;
            }
            if(p.phase == 2) continue; // reserved during MAGIC01's first 42 updates
            if(p.phase == 0) {
                if(p.remaining) --p.remaining;
                if(p.remaining) continue;
                p.x = battle.sides[int(p.target_side)].x;
                p.y = battle_commander_y;
                p.phase = 1;
            }
            const bool callback_timer_was_active=p.height>0;
            if(p.source_script==2626 && p.scatter<6) {
                // GLOBAL2505 creates one 3920/3921 object, calls native21
                // immediately, then repeats it five more times while waiting
                // one update between calls. The handle follows its selected
                // general for those six placements. native38(16,1) then
                // starts a 16-update object timer; #9999 in Things.ini is
                // only the sequence hold and does not override that timer.
                p.x=battle.sides[int(p.target_side)].x;
                p.y=battle_commander_y;
                ++p.scatter;
                if(p.scatter==6) p.height=16;
            }
            const int target_side = int(p.target_side);
            const int column = grid_column(p.x);
            const int row = grid_row(p.y);
            bool soldier_occupies_cell = false;
            if(column >= 0 && column < battle_grid_columns && row >= 0 && row < battle_grid_rows) {
                for(int index=cell_head(column,row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                    if(target.hp <= 0) continue;
                    soldier_occupies_cell = true;
                    if(target.side == target_side) {
                        auto& force = battle.sides[target_side];
                        target.hp = 0;
                        --force.soldiers;
                        if(!target.rear) --force.front;
                    }
                    break; // native cell lookup returns one occupied actor
                }
            }
            if(!soldier_occupies_cell) {
                auto& general = battle.sides[target_side];
                if(general.hp > 0 && grid_column(general.x) == column &&
                   grid_row(battle_commander_y) == row) {
                    // GLOBAL 2520 consumes global44, then changes it to 1.
                    // General immunity suppresses damage but not the callback.
                    apply_magic_general_callback(p.source_script,target_side,1);
                }
            }
            if(p.height > 0 && (p.source_script!=2626 || callback_timer_was_active) &&
               !--p.height) p.active = 0;
            continue;
        }
        if(p.magic == 10) {
            if(p.phase==2) continue; // held for a later GLOBAL6000 burst
            if(p.target_side < 0 || p.target_side > 1) {
                p.active=0;
                continue;
            }
            if(p.source_script==2665) {
                // demake ShenGuiLuanWu calls GeneralController.OnDamage
                // directly.  Its M022 visual must never kill a soldier or
                // use the normal cell-occupancy callback path.
                if(p.collision_enabled) {
                    apply_magic_general_callback(p.source_script,
                                                 int(p.target_side),1);
                    p.collision_enabled=0;
                }
                p.active=0;
                continue;
            }
            const int column=grid_column(p.x), row=grid_row(p.y);
            bool occupied=false;
            if(column>=0 && column<battle_grid_columns && row>=0 && row<battle_grid_rows) {
                for(int index=cell_head(column,row); index>=0; index=cell_next[index]) {
                    auto& target=battle.soldiers[index];
                    if(target.hp<=0) continue;
                    occupied=true;
                    if(target.side==p.target_side) {
                        auto& force=battle.sides[int(p.target_side)];
                        target.hp=0;
                        --force.soldiers;
                        if(!target.rear) --force.front;
                    }
                    break; // native collision reads only the first process in a cell
                }
                if(!occupied && !p.scatter) {
                    auto& target=battle.sides[int(p.target_side)];
                    if(target.hp>0 && grid_column(target.x)==column &&
                       grid_row(battle_commander_y)==row) {
                        apply_magic_general_callback(p.source_script,int(p.target_side),1);
                        p.scatter=1; // GLOBAL2520 removes this object's general mask
                    }
                }
            }
            if(p.remaining) --p.remaining;
            if(!p.remaining) p.active=0;
            continue;
        }
        if(p.magic>=28 && p.magic<=30) {
            if(p.phase==2) continue; // pre-reserved for its stone's impact.
            if(p.magic==30) {
                // MAGIC01 3803 sets native23 velocity, then removes the
                // 381x flame itself after RNG(30,40) one-update waits.
                // Things.ini sequences3810-3814 set OF_NOGRAVITY. MAGIC01
                // 3803 supplies a fixed native23 vertical speed but no
                // native29 gravity value, so z advances linearly. The VM's
                // opcode34 only shrinks collision extents; the source cell
                // lookup still checks the moved object's single grid cell.
                int x_remainder=int(p.movement_remainder)+p.dx;
                const int x_step=x_remainder/8;
                p.movement_remainder=int8_t(x_remainder-x_step*8);
                int y_remainder=int(p.scatter)-128+p.target_offset_x;
                const int y_step=y_remainder/8;
                p.scatter=uint8_t(y_remainder-y_step*8+128);
                const int age=int(p.owner_index)+1;
                const int z_eighth=40*int8_t(p.attacker_rear)*age/12;
                p.owner_index=uint8_t(age);
                p.x=int16_t(clamp(int(p.x)+x_step,0,battlefield_width-1));
                p.y=int16_t(clamp(int(p.y)+y_step,0,battle_grid_max_y));
                p.height=int16_t(maximum(0,z_eighth/8));
                const int column=grid_column(p.x), row=grid_row(p.y);
                if(p.collision_enabled && column>=0 && column<battle_grid_columns &&
                   row>=0 && row<battle_grid_rows && p.height<40) {
                    // Native cell lookup returns its first occupant only;
                    // soldiers mask general hits in the same grid cell.
                    bool occupied=false;
                    for(int index=cell_head(column,row);index>=0;index=cell_next[index])
                        if(battle.soldiers[index].hp>0) {occupied=true;break;}
                    if(!occupied && row==grid_row(battle_commander_y)) {
                        for(int side=0;side<2;++side) {
                            auto& general=battle.sides[side];
                            if(general.hp>0 && grid_column(general.x)==column) {
                                const uint8_t callback_mode=
                                    battle.magic_callback_damage>1 ? 2 : 1;
                                apply_magic_general_callback(p.source_script,side,
                                                             callback_mode);
                                spawn_3103_hit_visual(p.source_script,side,-1);
                                // The native 3103 callback clears both
                                // general-collision bits after this hit.
                                p.collision_enabled=0;
                                break; // one occupied cell exposes one general.
                            }
                        }
                    }
                }
                if(p.remaining && !--p.remaining) p.active=0;
            } else {
                ++p.height;
                if(p.remaining && !--p.remaining) p.active=0;
            }
            continue;
        }
        if(p.magic == 11) {
            // GLOBAL 3700 schedules delayed stone-fire objects. The object
            // flies toward its native269 soldier sample (or the scattered
            // native297 fallback point), then dispatches a collision callback.
            if(p.phase == 2) continue; // reserved while the cast is winding up
            if(p.phase == 3) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    if(p.source_script==2622 || p.source_script==2623) {
                        // MAGIC01 3802 starts a short A1/A2 impact object and
                        // ten 3810-3814 field objects. The original script
                        // draws each frame/offset from RNG; retain those calls
                        // and their ranges while activating this strike's
                        // reserved display records.
                        const uint8_t impact_lifetime=uint8_t(84+roll(172));
                        for(auto& fire : battle.projectiles) {
                            if(!fire.active || fire.phase!=2 ||
                               fire.source_script!=p.source_script ||
                               fire.target_offset_x!=p.target_offset_x ||
                               fire.owner_index!=p.kind || fire.magic<28 || fire.magic>30)
                                continue;
                            fire.x=p.x;
                            fire.y=p.y;
                            fire.height=0;
                            fire.phase=1;
                            fire.collision_enabled=uint8_t(fire.magic==30);
                            if(fire.magic==28) fire.remaining=31; // six #5 frames plus terminal update.
                            else if(fire.magic==29) fire.remaining=impact_lifetime;
                            else {
                                fire.kind=uint8_t(roll(5));
                                // MAGIC01 3802 passes RNG(0,255) as native17's
                                // orientation and literal0 as z offset. The
                                // child stays at the impact point; 3803 reuses
                                // the stored orientation for native23 motion.
                                const int angle=roll(256);
                                const int speed=4+roll(9);
                                const int vertical_speed=-2+roll(11);
                                fire.dx=int16_t(magic_planar_particle_velocity(speed,angle,0));
                                fire.target_offset_x=int16_t(
                                    magic_planar_particle_velocity(speed,angle,1));
                                fire.attacker_rear=uint8_t(int8_t(vertical_speed));
                                fire.movement_remainder=0;
                                fire.height_remainder=0;
                                fire.scatter=128;
                                fire.owner_index=0; // vertical-flight age.
                                fire.remaining=uint8_t(30+roll(11));
                            }
                        }
                    }
                    if(p.source_script==2618 || p.source_script==2622 ||
                       p.source_script==2623) {
                        // Death #2, NULSHAPE, !3712 starts one central
                        // sequence3711 object and ten independently sampled
                        // sequences3710-3714. Release the stone slot for the
                        // first child, while retaining the central break object.
                        const int source_script=p.source_script;
                        const int impact_x=p.x, impact_y=p.y;
                        const int8_t target_side=p.target_side;
                        p={};
                        p.active=1;
                        p.magic=33;
                        p.kind=1; // Things sequence3711 uses M007Break2.
                        p.source_script=int16_t(source_script);
                        p.target_side=target_side;
                        p.x=int16_t(impact_x);
                        p.y=int16_t(impact_y);
                        // Sequence3711 holds M007Break2 for #9999 updates.
                        // That exceeds any battle, so keep this static object
                        // alive until the battle projectile pool is cleared.
                        p.remaining=0;
                        uint8_t frames[10], angles[10];
                        for(int shard=0;shard<10;++shard) {
                            frames[shard]=uint8_t(roll(5));
                            angles[shard]=uint8_t(roll(256));
                        }
                        for(int shard=0;shard<10;++shard) {
                            const int frame=frames[shard]; // sequence3710-3714.
                            const int angle=angles[shard]; // native17 orientation.
                            const int speed=4+roll(9); // native23 x/y speed.
                            const int vertical_speed=-2+roll(11);
                            const int lifetime=30+roll(11); // native38(16,3).
                            Projectile* fragment=nullptr;
                            for(auto& candidate : battle.projectiles)
                                if(!candidate.active) { fragment=&candidate; break; }
                            if(!fragment) continue;
                            *fragment={};
                            fragment->active=1;
                            fragment->magic=32;
                            fragment->kind=uint8_t(frame);
                            fragment->source_script=int16_t(source_script);
                            fragment->target_side=target_side;
                            fragment->x=int16_t(impact_x);
                            fragment->y=int16_t(impact_y);
                            fragment->dx=int16_t(
                                magic_planar_particle_velocity(speed,angle,0));
                            fragment->target_offset_x=int16_t(
                                magic_planar_particle_velocity(speed,angle,1));
                            fragment->attacker_rear=uint8_t(int8_t(vertical_speed));
                            fragment->remaining=uint8_t(lifetime);
                        }
                    } else {
                        p.active = 0;
                    }
                }
                continue;
            }
            if(p.target_side < 0 || p.target_side > 1) {
                p.active = 0;
                continue;
            }
            if(p.phase == 0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    // MAGIC01 3705 reads the selected actor's handle again
                    // after its 80+RNG(1,30) wait, then places the stone
                    // relative to that actor. The target may have moved
                    // since GLOBAL 3700 sampled it for the child script.
                    if(p.owner_index < 200) {
                        const auto& target = battle.soldiers[int(p.target_side)*200+int(p.owner_index)];
                        if(target.hp > 0) {
                            const int direction = p.dx < 0 ? -1 : 1;
                            p.x = int16_t(clamp(int(target.x)-direction*(284*battle_grid_column_width/96),
                                                0,battlefield_width-1));
                            p.y = target.y;
                        }
                    }
                    p.phase = 1;
                }
                continue;
            }

            // native24 moves about 22.6 native units on both axes. Convert
            // horizontal distance by the 96-to-40 cell width and elevation
            // by the 72-to-30 cell height, carrying both fractional steps.
            // This keeps the rendered arc in the same world scale as the
            // collision grid while preserving the native flight duration.
            int horizontal_remainder=int(p.movement_remainder)+43;
            int horizontal_step=9;
            if(horizontal_remainder>=100) { horizontal_remainder-=100; ++horizontal_step; }
            p.movement_remainder=int8_t(horizontal_remainder);
            int fall_remainder=int(p.height_remainder)+43;
            int fall_step=9;
            if(fall_remainder>=100) { fall_remainder-=100; ++fall_step; }
            p.height_remainder=int8_t(fall_remainder);
            p.x = int16_t(int(p.x) + (p.dx<0 ? -horizontal_step : horizontal_step));
            p.height = int16_t(maximum(0,int(p.height)-fall_step));
            const int target_side = int(p.target_side);
            // Stone objects 3702/3703 carry OF_BOMB. FUN_0040b01c sends a
            // bomb to state 0x20 once native z falls below 1, before it does
            // another occupancy lookup. Their Things.ini Death sequence is
            // two updates, so ground impact uses the same terminal phase as
            // an accepted hit and cannot claim a unit on the landing tick.
            if(p.height<=0) {
                p.phase=3;
                p.remaining=2;
                continue;
            }
            // FUN_0040b01c skips MagicObjectProcess grid collisions while
            // native z >= 0x600000 (96 native units). Convert that threshold
            // through the same 72-native-to-30-world row scale as the arc.
            // Position has already advanced this update, matching the native
            // object update before its collision callback.
            constexpr int collision_height_limit=battle_grid_row_height*96/72;
            if(p.height>=collision_height_limit) continue;
            const int column = grid_column(p.x), row = grid_row(p.y);
            bool occupied = false, hit = false;
            int hit_soldier=-1;
            if(column >= 0 && column < battle_grid_columns && row >= 0 && row < battle_grid_rows) {
                for(int index=cell_head(column,row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                    if(target.hp <= 0) continue;
                    occupied = true;
                    if(target.side == target_side) {
                        auto& force = battle.sides[target_side];
                        target.hp = 0;
                        --force.soldiers;
                        if(!target.rear) --force.front;
                        hit = true;
                        hit_soldier=index%200;
                    }
                    break; // native collision lookup observes the first process in a cell
                }
                if(!occupied) {
                    auto& general = battle.sides[target_side];
                    if(general.hp > 0 && grid_column(general.x) == column &&
                       grid_row(battle_commander_y) == row) {
                        const uint8_t callback_mode =
                            (p.source_script == 2622 || p.source_script == 2623) &&
                            battle.magic_callback_damage > 1 ? 2 : 1;
                        apply_magic_general_callback(p.source_script,target_side,callback_mode);
                        hit = true;
                        hit_soldier=0xff;
                    }
                }
            }
            if(hit) {
                spawn_3103_hit_visual(p.source_script,target_side,hit_soldier);
                p.phase = 3;
                p.remaining = 2;
            } else if(p.x < 0 || p.x >= battlefield_width) {
                if(p.source_script==2622 || p.source_script==2623) {
                    p.phase=3; // Death of sequence3802 still starts its fire field.
                    p.remaining=2;
                } else p.active = 0;
            }
            continue;
        }
        if(p.magic == 7) {
            if(p.target_side < 0 || p.target_side > 1) {
                p.active = 0;
                continue;
            }
            if(p.phase == 0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) p.phase = 1;
                continue;
            }
            if(p.height <= 0) {
                p.active = 0;
                continue;
            }
            // GLOBAL 2568 places sequence 2500/4000 at x+700 in native
            // units, then native24 gives it an 8-pixel horizontal velocity.
            // The native cell is 96 pixels wide, so preserve 10/3 GBA units
            // per update and retain the original outer-row lanes.
            int remainder = int(p.movement_remainder) + (p.dx > 0 ? 10 : -10);
            const int step = remainder / 3;
            p.movement_remainder = int8_t(remainder - step*3);
            p.x = int16_t(int(p.x) + step);
            if(p.x < 0 || p.x >= battlefield_width) {
                p.active = 0;
                continue;
            }
            const int column = grid_column(p.x);
            const int row = int(p.y) / battle_grid_row_height;
            bool callback_accepted=false;
            if(column >= 0 && column < battle_grid_columns && row >= 0 && row < battle_grid_rows) {
                for(int index=cell_head(column,row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                    if(target.hp <= 0) continue;
                    if(target.side == p.target_side) {
                        auto& force = battle.sides[int(target.side)];
                        target.hp = 0;
                        --force.soldiers;
                        if(!target.rear) --force.front;
                        callback_accepted=true;
                    }
                    // Native cell lookup returns one process. The beam keeps
                    // moving after the instant soldier-death event.
                    break;
                }
                if(!callback_accepted) {
                    const auto& general=battle.sides[int(p.target_side)];
                    if(general.hp>0 && grid_column(general.x)==column &&
                       grid_row(battle_commander_y)==row)
                        callback_accepted=true;
                }
            }
            if(callback_accepted)
                spawn_3904_particles(p.source_script,p.x,p.y,20);
            --p.height;
            if(!p.height) p.active = 0;
            continue;
        }
        if(p.magic == 8) {
            if(p.target_side < 0 || p.target_side > 1) {
                p.active = 0;
                continue;
            }
            if(p.phase == 0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    p.phase = 1;
                    p.x = battle.sides[int(p.target_side)].x;
                    p.y = battle_commander_y;
                    // GLOBAL6230 starts two GLOBAL2565 objects using Things
                    // 3920/3921 (M009 Thunder B1/B2). Each runs five native21
                    // collision updates, then native38(16,1) owns its final
                    // 16-update visual lifetime.
                    p.scatter=0; // native21 placement count for collision updates.
                    p.remaining=21;
                }
                continue;
            }
            if(p.phase == 2) {
                if(p.scatter<255) ++p.scatter;
                if(p.remaining) --p.remaining;
                if(!p.remaining) p.active = 0;
                continue;
            }
            if(p.height <= 0) {
                p.phase = 2;
                p.remaining=16;
                continue;
            }
            const int target_side = int(p.target_side);
            auto& general = battle.sides[target_side];
            // GLOBAL2565 calls native21 against the live general handle on
            // each of its five collision updates; it stops following when
            // native38(16,1) starts the static visual tail.
            p.x=general.x;
            p.y=battle_commander_y;
            const int column = grid_column(p.x);
            const int row = grid_row(p.y);
            bool occupied = false;
            if(column >= 0 && column < battle_grid_columns && row >= 0 && row < battle_grid_rows) {
                for(int index=cell_head(column,row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                    if(target.hp <= 0) continue;
                    occupied = true;
                    if(target.side == target_side) {
                        auto& force = battle.sides[target_side];
                        target.hp = 0;
                        --force.soldiers;
                        if(!target.rear) --force.front;
                    }
                    break;
                }
            }
            if(!occupied && general.hp > 0) {
                // MAGIC01 3905 dispatches GLOBAL 2520. The wrapper seeds
                // global44=30; GLOBAL 2520 changes it to 1 after its first
                // accepted general callback, shared by both thunder objects.
                if(!(p.kind&1)) {
                    apply_magic_general_callback(p.source_script,target_side,1);
                    // MAGIC01 3905 clears this object's collision mask, then
                    // starts nine 3904 child scripts in three groups of
                    // three, with eight WAIT updates between groups. Each
                    // 3904 emits twenty M08B particles. Defer later groups
                    // so their random draws and pool use happen at launch.
                    spawn_3904_particles(p.source_script,p.x,p.y,60);
                    for(int group_index=1;group_index<=2;++group_index) {
                        const int delay=group_index*8;
                        Projectile* group=nullptr;
                        for(auto& candidate : battle.projectiles)
                            if(!candidate.active) { group=&candidate; break; }
                        if(!group) break;
                        *group={};
                        group->active=1;
                        group->magic=24;
                        group->kind=1;
                        group->remaining=uint8_t(delay);
                        group->source_script=p.source_script;
                        group->x=p.x;
                        group->y=p.y;
                    }
                    p.kind|=1;
                }
            }
            --p.height;
            if(p.scatter<255) ++p.scatter;
            if(!p.height) {
                p.phase=2;
                p.remaining=16;
            }
            continue;
        }
        if(p.magic == 5) {
            update_cyclone_projectile(p);
            continue;
        }
        if(p.magic == 6) {
            if(p.phase == 0) {
                if(p.remaining) --p.remaining;
                if(p.remaining) continue;
                // GLOBAL 2506 creates the collision object after its random
                // delay, then calls native21 five times (counter4..0) on the
                // target, with one WAIT1 between placements.
                // The associated 3910..3913 visual stays alive for another
                // native38(16,1) timer after that callback window.
                p.phase = 1;
                p.remaining = p.owner_index<200 ? 5 : 4;
                p.height = 0; // lifetime age; M009's first frame is immediate.
                continue;
            }
            if(p.phase == 1) {
                if(p.target_side >= 0 && p.target_side <= 1) {
                    // GLOBAL2506 follows its selected soldier with native21.
                    // Excess tier strikes use GLOBAL2507 and stay at their
                    // random battlefield coordinates instead.
                    if(p.owner_index<200) {
                        const auto& target = battle.soldiers[int(p.target_side)*200+int(p.owner_index)];
                        p.x=target.x;
                        p.y=target.y;
                    }
                    const int column = grid_column(p.x);
                    const int row = grid_row(p.y);
                    if(column >= 0 && column < battle_grid_columns && row >= 0 && row < battle_grid_rows) {
                        // Native collision dispatch checks the occupied cell,
                        // not the selected target's actor handle. It returns
                        // the first actor in that cell for this update.
                        for(int index=cell_head(column,row); index>=0; index=cell_next[index]) {
                            auto& occupant = battle.soldiers[index];
                            if(occupant.hp <= 0) continue;
                            if(occupant.side == p.target_side) {
                                auto& force = battle.sides[int(p.target_side)];
                                occupant.hp = 0;
                                --force.soldiers;
                                if(!occupant.rear) --force.front;
                            }
                            break;
                        }
                    }
                }
                if(p.height<32767) ++p.height;
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    p.phase = 2;
                    p.remaining = 16;
                }
                continue;
            }
            if(p.height<32767) ++p.height;
            if(p.remaining) --p.remaining;
            if(!p.remaining) p.active = 0;
            continue;
        }
        if(p.magic == 1 && p.phase == 0) {
            if(p.remaining) --p.remaining;
            if(p.remaining) continue;
            p.phase = 1;
            continue;
        }
        if(p.magic == 3) {
            if(p.target_side < 0 || p.target_side > 1) {
                p.active = 0;
                continue;
            }
            if(p.phase == 0) {
                if(p.remaining) --p.remaining;
                if(p.remaining) continue;
                p.phase = 1;
                // GLOBAL 2503 waits RNG(1,30); this delay is independent for
                // each child script in the native scheduler.
                p.remaining = uint8_t(1 + roll(30));
                continue;
            }
            if(p.phase == 1) {
                if(p.remaining) --p.remaining;
                if(p.remaining) continue;
                static constexpr int8_t mine_dx[256] = {
    96,96,96,96,96,95,95,95,94,94,93,93,92,91,90,90,
    89,88,87,86,85,84,82,81,80,78,77,76,74,73,71,70,
    68,66,64,63,61,59,57,55,53,51,49,47,45,43,41,39,
    37,35,32,30,28,26,23,21,19,16,14,12,9,7,5,2,
    0,-2,-5,-7,-9,-12,-14,-16,-19,-21,-23,-26,-28,-30,-32,-35,
    -37,-39,-41,-43,-45,-47,-49,-51,-53,-55,-57,-59,-61,-63,-64,-66,
    -68,-70,-71,-73,-74,-76,-77,-78,-80,-81,-82,-84,-85,-86,-87,-88,
    -89,-90,-90,-91,-92,-93,-93,-94,-94,-95,-95,-95,-96,-96,-96,-96,
    -96,-96,-96,-96,-96,-95,-95,-95,-94,-94,-93,-93,-92,-91,-90,-90,
    -89,-88,-87,-86,-85,-84,-82,-81,-80,-78,-77,-76,-74,-73,-71,-70,
    -68,-66,-64,-63,-61,-59,-57,-55,-53,-51,-49,-47,-45,-43,-41,-39,
    -37,-35,-32,-30,-28,-26,-23,-21,-19,-16,-14,-12,-9,-7,-5,-2,
    0,2,5,7,9,12,14,16,19,21,23,26,28,30,32,35,
    37,39,41,43,45,47,49,51,53,55,57,59,61,63,64,66,
    68,70,71,73,74,76,77,78,80,81,82,84,85,86,87,88,
    89,90,90,91,92,93,93,94,94,95,95,95,96,96,96,96,
};
                static constexpr int8_t mine_dy[256] = {
    0,2,5,7,9,12,14,16,19,21,23,26,28,30,32,35,
    37,39,41,43,45,47,49,51,53,55,57,59,61,63,64,66,
    68,70,71,73,74,76,77,78,80,81,82,84,85,86,87,88,
    89,90,90,91,92,93,93,94,94,95,95,95,96,96,96,96,
    96,96,96,96,96,95,95,95,94,94,93,93,92,91,90,90,
    89,88,87,86,85,84,82,81,80,78,77,76,74,73,71,70,
    68,66,64,63,61,59,57,55,53,51,49,47,45,43,41,39,
    37,35,32,30,28,26,23,21,19,16,14,12,9,7,5,2,
    0,-2,-5,-7,-9,-12,-14,-16,-19,-21,-23,-26,-28,-30,-32,-35,
    -37,-39,-41,-43,-45,-47,-49,-51,-53,-55,-57,-59,-61,-63,-64,-66,
    -68,-70,-71,-73,-74,-76,-77,-78,-80,-81,-82,-84,-85,-86,-87,-88,
    -89,-90,-90,-91,-92,-93,-93,-94,-94,-95,-95,-95,-96,-96,-96,-96,
    -96,-96,-96,-96,-96,-95,-95,-95,-94,-94,-93,-93,-92,-91,-90,-90,
    -89,-88,-87,-86,-85,-84,-82,-81,-80,-78,-77,-76,-74,-73,-71,-70,
    -68,-66,-64,-63,-61,-59,-57,-55,-53,-51,-49,-47,-45,-43,-41,-39,
    -37,-35,-32,-30,-28,-26,-23,-21,-19,-16,-14,-12,-9,-7,-5,-2,
};
                const int angle = roll(256);
                const int caster_side = 1 - int(p.target_side);
                const auto& caster = battle.sides[caster_side];
                p.x = int16_t(caster.x + mine_dx[angle] * battle_grid_column_width / 96);
                p.y = int16_t(battle_commander_y + mine_dy[angle] * battle_grid_row_height / 72);
                // GLOBAL 2503 passes this angle to native17 as the object's
                // orientation, then reuses it for native21's polar placement.
                // The sequence and its collision proxy start on object update.
                // GLOBAL wrappers pass 110 through local2; GLOBAL2503 draws
                // 0..110 and native21 adds that fifth argument to z (+0x10).
                // Keep the source's full vertical range before the <96-native
                // collision gate, so high mines can remain non-colliding.
                p.height = int16_t(roll(111) * battle_grid_column_width / 96);
                p.phase = 3;
                p.remaining = uint8_t(original::minefield_visual_lifetime);
                continue;
            }

            if(p.height < battle_grid_column_width) {
                const int mine_column = grid_column(p.x);
                const int mine_row = grid_row(p.y);
                bool hit = false;
                if(mine_column >= 0 && mine_column < battle_grid_columns &&
                   mine_row >= 0 && mine_row < battle_grid_rows) {
                    // FUN_0043c227 returns one process for the cell. Soldier
                    // processes are checked first here, then the center-lane
                    // commander.
                    for(int index=cell_head(mine_column,mine_row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                        if(target.hp <= 0 || target.side != p.target_side) continue;
                        auto& force = battle.sides[int(p.target_side)];
                        target.hp = 0;
                        --force.soldiers;
                        if(!target.rear) --force.front;
                        hit = true;
                        break;
                    }
                    if(!hit) {
                        auto& target = battle.sides[int(p.target_side)];
                        if(target.hp > 0 && grid_column(target.x) == mine_column &&
                           grid_row(battle_commander_y) == mine_row) {
                            apply_magic_general_callback(p.source_script,int(p.target_side),1);
                        }
                    }
                }
            }
            if(p.remaining) --p.remaining;
            if(!p.remaining) p.active = 0;
            continue;
        }
        if(p.magic == 4) {
            if(p.target_side < 0 || p.target_side > 1) {
                p.active = 0;
                continue;
            }
            if(p.phase == 33) {
                // The sequence4401 child mirrors the elapsed wait until
                // MAGIC01 4400 raises shared var38, then waits that many
                // updates again before the 12-update downward step.
                if(p.scatter) --p.scatter;
                if(p.remaining) --p.remaining;
                if(!p.remaining) { p.phase=34; p.remaining=12; }
                continue;
            }
            if(p.phase == 34) {
                if(p.scatter) --p.scatter;
                else --p.height; // native24(0,64,-1) while the child waits12.
                if(p.remaining) --p.remaining;
                if(!p.remaining) { p.phase=35; p.remaining=60; }
                continue;
            }
            if(p.phase == 35) {
                // The native17 global36 argument controls facing, not timing.
                // Then native24(0,64,16) sends the static child upward for
                // native31(60)'s object lifetime.
                p.height=int16_t(p.height+16);
                if(p.remaining && !--p.remaining) p.active=0;
                continue;
            }
            if(p.phase == 0) {
                if(p.remaining) {
                    --p.remaining;
                    if(p.remaining) continue;
                }
                p.phase = 1;
            }
            // MAGIC01 4402 waits 32 updates before replacing sequence4400
            // with the separate static sequence4401 image. Collision only
            // applies below z96 (the first 16 updates), but the animated
            // sword remains visible for the full 32-update ascent.
            if(p.phase > 32) {
                const int count=original::sword_wave_projectile_count(p.source_script);
                const int remaining_siblings=maximum(1,count-int(p.owner_index));
                p.phase=33;
                p.remaining=uint8_t(2*remaining_siblings);
                p.height=128;
                continue;
            }
            p.height = int16_t(64 + 2*(int(p.phase)-1));
            if(p.height < 96) {
                const int wave_column = grid_column(p.x);
                const int wave_row = grid_row(p.y);
                if(wave_column >= 0 && wave_column < battle_grid_columns &&
                   wave_row >= 0 && wave_row < battle_grid_rows) {
                    // FUN_0040b01c's mask accepts opposing generals and
                    // soldiers. Its soldier event removes the occupied unit
                    // immediately; one process lookup exposes one occupant.
                    for(int index=cell_head(wave_column,wave_row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                        if(target.hp <= 0 || grid_column(target.x) != wave_column ||
                           grid_row(target.y) != wave_row) continue;
                        if(target.side == p.target_side) {
                            auto& force = battle.sides[int(p.target_side)];
                            target.hp = 0;
                            --force.soldiers;
                            if(!target.rear) --force.front;
                        }
                        break;
                    }
                }
            }
            ++p.phase;
            continue;
        }
        if(p.magic==17) {
            if(p.remaining) --p.remaining;
            if(!p.remaining) p.active=0;
            continue;
        }
        if(p.magic == 16) {
            // Things 3102 is a separate non-colliding M002 object with the
            // same launch vector as arrow3003/3006 and an 80-update lifetime.
            // It continues after the colliding arrow stops.
            if(p.phase==0) {
                if(p.remaining) {
                    --p.remaining;
                    if(p.remaining) continue;
                }
                p.phase=1;
                p.remaining=80;
                continue;
            }
            if(p.remaining) --p.remaining;
            if(!p.remaining) {
                p.active=0;
                continue;
            }
            int x_remainder=int(p.movement_remainder)+(p.dx<0 ? -226 : 226);
            const int x_step=x_remainder/24;
            p.movement_remainder=int8_t(x_remainder-x_step*24);
            p.x=int16_t(p.x+x_step);
            int z_remainder=int(p.height_remainder)-226;
            const int z_step=z_remainder/24;
            p.height_remainder=int8_t(z_remainder-z_step*24);
            p.height=int16_t(p.height+z_step);
            if(p.x<grid_origin_x-battle_grid_column_width/2 ||
               p.x>=grid_origin_x+battle_grid_columns*battle_grid_column_width+
                    battle_grid_column_width/2)
                p.active=0;
            continue;
        }
        if(p.magic==16) {
            // Things sequence3102 is the independent, non-colliding M002
            // fire image paired with an M001 arrow. It inherits that arrow's
            // native24 velocity and its own 80-update lifetime. A later-slot
            // child waits one update after construction before it starts.
            if(p.phase==0) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) {
                    p.phase=1;
                    p.remaining=80;
                }
                continue;
            }
            int x_remainder=int(p.movement_remainder)+(p.dx<0 ? -226 : 226);
            const int x_step=x_remainder/24;
            p.movement_remainder=int8_t(x_remainder-x_step*24);
            p.x=int16_t(p.x+x_step);
            int z_remainder=int(p.height_remainder)-226;
            const int z_step=z_remainder/24;
            p.height_remainder=int8_t(z_remainder-z_step*24);
            p.height=int16_t(p.height+z_step);
            if(p.x<grid_origin_x-battle_grid_column_width/2 ||
               p.x>=grid_origin_x+battle_grid_columns*battle_grid_column_width+
                    battle_grid_column_width/2 ||
               (p.remaining && !--p.remaining))
                p.active=0;
            continue;
        }
        if(p.magic == 2) {
            if(p.phase==0 && p.source_script>=2600 && p.source_script<=2605) {
                // MAGIC01 3100 reaches its damaging-arrow child setup only
                // after WAIT20+WAIT30+WAIT60. Start each saved 81-110 child
                // delay after that barrier, not from cast creation.
                if(p.kind) {
                    // height is unused until flight. Keep the parent barrier
                    // private: a later cast can replace battle.effect_ticks.
                    if(p.height>0) --p.height;
                    if(!p.height) p.kind=0;
                    continue;
                }
            }
            if(p.phase == 0 && p.scatter == 2 && p.source_script == 2664) {
                if(p.remaining) {
                    --p.remaining;
                    if(p.remaining) continue;
                }
                if(p.target_side < 0 || p.target_side > 1) {
                    p.active = 0;
                    continue;
                }
                int live_indices[200];
                int live_count = 0;
                for(int i=0; i<200; ++i)
                    if(battle.soldiers[int(p.target_side)*200+i].hp>0) live_indices[live_count++]=i;
                const int targeted_count = minimum(20,live_count);
                const int anchor_x = native297_anchor_x(battle,int(p.target_side));
                int child_number = 0;
                for(auto& child : battle.projectiles) {
                    if(!child.active || child.magic!=2 || child.phase!=0 ||
                       child.scatter!=2 || child.source_script!=2664 ||
                       child.target_side!=p.target_side) continue;
                    if(child_number < targeted_count) {
                        const int target_rank = roll(live_count);
                        child.owner_index = uint8_t(live_indices[target_rank]);
                        child.scatter = 0;
                    } else {
                        child.owner_index = 0xff;
                        child.scatter = 1;
                        child.x = int16_t(anchor_x+(roll(641)-320)*battle_grid_column_width/96);
                        child.y = int16_t((150+roll(501))*battle_grid_row_height/72);
                    }
                    const int delay = 81+roll(30);
                    // Later siblings are visited later in this same tick and
                    // consume one countdown step immediately.
                    child.remaining = uint8_t(delay+(&child==&p ? 0 : 1));
                    ++child_number;
                }
                continue;
            }
            if(p.phase == 0) {
                if(p.remaining) {
                    --p.remaining;
                    if(p.remaining) continue;
                }
                const int target_index = p.owner_index;
                if(p.target_side < 0 || p.target_side > 1) {
                    p.active = 0;
                    continue;
                }
                const int direction = p.dx < 0 ? -1 : 1;
                if(target_index == 0xff) {
                    if(!p.scatter) {
                        // Invalid native target: the child remains at the
                        // parent object's position, then native40 offsets it
                        // behind the caster before flight toward the enemy.
                        const int caster_x = battle.sides[1-int(p.target_side)].x;
                        p.x = int16_t(caster_x - direction*284*battle_grid_column_width/96);
                        p.y = battle_commander_y;
                    }
                } else {
                    if(target_index >= 200) {
                        p.active = 0;
                        continue;
                    }
                    const auto& aimed = battle.soldiers[int(p.target_side)*200+target_index];
                    if(aimed.hp <= 0) { p.active = 0; continue; }
                    // Native 6203/6204 start 284px from the selected soldier;
                    // direct 3003/3004 use a 260px offset plus ±320px scatter.
                    p.x = int16_t(int(aimed.x) - direction * (p.scatter ? 260 : 284)*battle_grid_column_width/96 + p.target_offset_x);
                    if(!p.scatter) p.y = aimed.y;
                }
                p.height = int16_t(260*battle_grid_column_width/96);
                p.height_remainder = 0;
                p.movement_remainder = 0;
                p.phase = 1;
                p.collision_enabled = 1;
                if(p.source_script>=2603 && p.source_script<=2605) {
                    const auto arrow_slot=&p-battle.projectiles;
                    for(int flame_slot=0;flame_slot<battle_projectile_slots;++flame_slot) {
                        auto& flame=battle.projectiles[flame_slot];
                        if(flame.active) continue;
                        flame={};
                        flame.active=1;
                        flame.magic=16;
                        // Earlier slots have already updated this tick; later
                        // slots get a one-tick constructor wait before moving.
                        flame.phase=flame_slot<arrow_slot ? 1 : 0;
                        flame.remaining=flame.phase ? 80 : 1;
                        flame.dx=p.dx;
                        flame.x=p.x;
                        flame.y=p.y;
                        flame.height=p.height;
                        flame.target_side=p.target_side;
                        flame.source_script=p.source_script;
                        break;
                    }
                } else if(p.source_script==2664) {
                    const auto arrow_slot=&p-battle.projectiles;
                    for(int flame_slot=0;flame_slot<battle_projectile_slots;++flame_slot) {
                        auto& flame=battle.projectiles[flame_slot];
                        if(flame.active) continue;
                        flame={};
                        flame.active=1;
                        flame.magic=16; // Things 3102: M002ArrowFire2 trail.
                        flame.phase=flame_slot<arrow_slot ? 1 : 0;
                        flame.remaining=flame.phase ? 80 : 1;
                        flame.dx=p.dx;
                        flame.x=p.x;
                        flame.y=p.y;
                        flame.height=p.height;
                        flame.target_side=p.target_side;
                        flame.source_script=2664;
                        break;
                    }
                }
                continue;
            }
            if(p.phase == 2) {
                if(p.remaining) --p.remaining;
                if(!p.remaining) p.active = 0;
                continue;
            }
            int x_remainder = int(p.movement_remainder) + (p.dx < 0 ? -226 : 226);
            const int x_step = x_remainder / 24;
            p.movement_remainder = int8_t(x_remainder - x_step * 24);
            p.x = int16_t(p.x + x_step);
            int z_remainder = int(p.height_remainder) - 226;
            const int z_step = z_remainder / 24;
            p.height_remainder = int8_t(z_remainder - z_step * 24);
            p.height = int16_t(p.height + z_step);
            if(p.x < grid_origin_x - battle_grid_column_width/2 ||
               p.x >= grid_origin_x + battle_grid_columns*battle_grid_column_width + battle_grid_column_width/2) {
                p.active = 0;
                continue;
            }
            // FUN_0040b01c ignores these effects until their native z is
            // below 96; height is stored in the same 40/96 world scale.
            if(p.height >= battle_grid_column_width) continue;
            const int projectile_column = grid_column(p.x);
            const int projectile_row = grid_row(p.y);
            bool accepted_hit = false;
            int hit_soldier=-1;
            bool hit_general=false;
            if(projectile_column >= 0 && projectile_column < 56 &&
               projectile_row >= 0 && projectile_row < battle_grid_rows) {
                for(int index=cell_head(projectile_column,projectile_row); index>=0; index=cell_next[index]) {
                    auto& target = battle.soldiers[index];
                    if(target.hp <= 0 || target.side != p.target_side) continue;
                    auto& force = battle.sides[int(p.target_side)];
                    target.hp = 0;
                    --force.soldiers;
                    if(!target.rear) --force.front;
                    hit_soldier=index-int(p.target_side)*200;
                    accepted_hit = true;
                    break;
                }
                if(!accepted_hit) {
                    auto& target = battle.sides[int(p.target_side)];
                    if(target.hp > 0 && grid_column(target.x) == projectile_column &&
                       grid_row(battle_commander_y) == projectile_row) {
                        const uint8_t callback_mode = p.source_script == 2664 ||
                            (p.source_script >= 2642 && p.source_script <= 2644) ? 2 :
                            original::magic_general_callback_next_base(p.source_script) ? 1 : 3;
                        apply_magic_general_callback(p.source_script,int(p.target_side),callback_mode);
                        hit_general=true;
                        accepted_hit = true;
                    }
                }
            }
            if(accepted_hit) {
                if(p.source_script>=2603 && p.source_script<=2605 &&
                   (hit_soldier>=0 || hit_general)) {
                    for(auto& fire : battle.projectiles) if(!fire.active) {
                        fire={};
                        fire.active=1;
                        fire.magic=17;
                        fire.remaining=76; // GLOBAL2504's 60-update follow plus native38(16,1).
                        fire.target_side=p.target_side;
                        fire.owner_index=hit_soldier>=0 ? uint8_t(hit_soldier) : 0xff;
                        fire.source_script=p.source_script;
                        break;
                    }
                }
                // Objects3003/3006 enter their four-update NULSHAPE death
                // sequence on the first accepted collision.
                p.collision_enabled = 0;
                p.phase = 2;
                p.remaining = 8;
            }
            continue;
        }
        update_fast_projectile(p,cell_heads,cell_next);
    }
}

[[gnu::noinline]] int demake_battle_location_x(int x) {
    return x * 2200 / (battlefield_width * 12);
}

[[gnu::noinline]] bool demake_general_hit_range(int source_x,int target_x,int direction) {
    const int source_cell=demake_battle_location_x(source_x);
    const int target_cell=demake_battle_location_x(target_x);
    return direction<0 ? target_cell>=source_cell-4 && target_cell<=source_cell :
                         target_cell>=source_cell && target_cell<=source_cell+4;
}

// SolidersController.RangeSolidersFightCheck rolls >90 out of [0,100),
// then waits .5..1 second before its next distant check. CheckCanFight
// bypasses that roll at melee distance. Fold the ranged check wait and
// post-animation one-second cooldown into the existing action scheduler.
[[gnu::noinline]] bool ranged_attack_trigger(Game& game,Soldier& soldier,int target_x,int force) {
    int check_wait=0;
    const int distance=demake_battle_location_x(target_x)-demake_battle_location_x(soldier.x);
    if(distance < -4 || distance > 4) {
        const bool release=game.roll(100)>90;
        check_wait=15+game.roll(16);
        if(!release) {soldier.action_ticks=uint16_t(check_wait);return false;}
    }
    soldier.action_ticks=uint16_t(original::soldier_attack_event_delay(force,soldier.side,soldier.rear)+30+check_wait);
    return true;
}

// The remake stores native FORCE token order. demake uses the troop-mask bit
// order from its armsName array; these are not the same for several units.
[[gnu::noinline]] int demake_soldier_type(int native_force) {
    constexpr uint8_t native_to_demake[11]={0,1,3,2,5,4,10,7,9,6,8};
    return native_force>=0 && native_force<11 ? native_to_demake[native_force] : 0;
}

[[gnu::noinline]] int demake_soldier_block_rate(int attack_type,int target_type) {
    switch(attack_type) {
    case 0:
        if(target_type==3 || target_type==4 || target_type==5) return 20;
        if(target_type==1 || target_type==2 || target_type==8) return 70;
        break;
    case 1:
        if(target_type==0 || target_type==6 || target_type==10) return 30;
        if(target_type==3 || target_type==4 || target_type==5) return 70;
        break;
    case 2:
        if(target_type==0 || target_type==7 || target_type==10) return 30;
        if(target_type==3 || target_type==4 || target_type==5) return 70;
        break;
    case 3: case 4: case 5:
        if(target_type==1 || target_type==2 || target_type==6 || target_type==8 || target_type==10) return 30;
        if(target_type==0 || target_type==7 || target_type==9) return 80;
        break;
    case 6:
        if(target_type==2 || target_type==8 || target_type==10) return 30;
        if(target_type==1 || target_type==3 || target_type==9) return 70;
        break;
    case 7:
        if(target_type==3 || target_type==4 || target_type==5) return 25;
        if(target_type==1 || target_type==2 || target_type==8) return 65;
        break;
    case 8:
        if(target_type==0 || target_type==7 || target_type==10) return 25;
        if(target_type==3 || target_type==4 || target_type==5) return 65;
        break;
    case 9:
        if(target_type==3 || target_type==4 || target_type==5) return 25;
        if(target_type==1 || target_type==2 || target_type==8) return 65;
        break;
    case 10: return 60;
    default: break;
    }
    return 50;
}

[[gnu::noinline]] bool demake_soldier_projectile_hit(Game& game,int attacker_side,int target_index) {
    auto& attacker=game.battle.sides[attacker_side];
    auto& defender=game.battle.sides[1-attacker_side];
    auto& target=game.battle.soldiers[target_index];
    const int block=demake_soldier_block_rate(demake_soldier_type(attacker.force),
                                             demake_soldier_type(defender.force));
    if(game.roll(100)<block) return false;
    target.hp=0;
    --defender.soldiers;
    if(!target.rear) --defender.front;
    return true;
}

[[gnu::noinline]] bool demake_soldier_damage_general(Game& game,int attacker_side) {
    const int defender_side=1-attacker_side;
    auto& defender=game.battle.sides[defender_side];
    const bool hit=game.roll(100)<=70-game.strength(defender.general)/5;
    if(hit && !game.battle.immunity_ticks[defender_side])
        defender.hp=int16_t(maximum(0,int(defender.hp)-2));
    return hit;
}

[[gnu::noinline]] void demake_soldier_apply_hit(Game& game,int side,int direction,int source_x,int y) {
    auto& own=game.battle.sides[side];
    auto& enemy=game.battle.sides[1-side];
    const int row=(y+battle_grid_row_height/2)/battle_grid_row_height;
    for(int index=(1-side)*200;index<(2-side)*200;++index) {
        auto& target=game.battle.soldiers[index];
        const int target_row=(int(target.y)+battle_grid_row_height/2)/battle_grid_row_height;
        if(target.hp<=0 || target_row!=row ||
           !demake_general_hit_range(source_x,target.x,direction)) continue;
        const int block=demake_soldier_block_rate(demake_soldier_type(own.force),
                                                 demake_soldier_type(enemy.force));
        if(game.roll(100)>=block) {
            target.hp=0;
            --enemy.soldiers;
            if(!target.rear) --enemy.front;
        }
    }
}

[[gnu::noinline]] bool demake_general_has_hit_target(const Game& game,int side,int direction) {
    const int row=(battle_commander_y+battle_grid_row_height/2)/battle_grid_row_height;
    for(int index=(1-side)*200;index<(2-side)*200;++index) {
        const auto& soldier=game.battle.soldiers[index];
        const int soldier_row=(int(soldier.y)+battle_grid_row_height/2)/battle_grid_row_height;
        if(soldier.hp>0 && soldier_row==row &&
           demake_general_hit_range(game.battle.sides[side].x,soldier.x,direction)) return true;
    }
    const auto& enemy=game.battle.sides[1-side];
    return enemy.hp>0 && demake_general_hit_range(game.battle.sides[side].x,enemy.x,direction);
}

[[gnu::noinline]] void demake_general_apply_hit(Game& game,int side,int direction) {
    auto& own=game.battle.sides[side];
    auto& enemy=game.battle.sides[1-side];
    const int row=(battle_commander_y+battle_grid_row_height/2)/battle_grid_row_height;
    for(int index=(1-side)*200;index<(2-side)*200;++index) {
        auto& target=game.battle.soldiers[index];
        const int target_row=(int(target.y)+battle_grid_row_height/2)/battle_grid_row_height;
        if(target.hp<=0 || target_row!=row ||
           !demake_general_hit_range(own.x,target.x,direction)) continue;
        // demake passes type 1000 to SolidersController.OnDamage: its default
        // block chance is 50%, independent of the general's STR.
        if(game.roll(100)>=50) {
            target.hp=0;
            --enemy.soldiers;
            if(!target.rear) --enemy.front;
        }
    }
    if(enemy.hp>0 && !game.battle.immunity_ticks[1-side] &&
       demake_general_hit_range(own.x,enemy.x,direction)) {
        const int threshold=original::commander_hit_threshold(game.strength(own.general),game.strength(enemy.general));
        if(game.roll(100)<=threshold) {
            const bool front_attack=direction==(side ? 1 : -1);
            const bool mighty=front_attack && (own.ai_action_flags&fighter_mighty_hit_flag);
            const int minimum_damage=game.strength(own.general)/12-(mighty ? 0 : 3);
            // Weak normal/back attacks can roll below zero. Damage must not
            // become healing; retain the roll so RNG timing is unchanged.
            const int damage=maximum(0,minimum_damage+game.roll(mighty ? 5 : 6));
            enemy.hp=int16_t(maximum(0,int(enemy.hp)-damage));
        }
    }
}

// Two commanders only: keep their attack scheduling in ROM, reserving fast
// RAM and stack headroom for the many-soldier simulation and audio.
[[gnu::noinline]] void update_commander_attack(Game& game,int side,int direction,bool ready,bool contact,bool casting) {
    auto& f=game.battle.sides[side];
    if(casting || f.hp<=0 || f.order==Order::retreat) return;
    int scan=f.attack_direction;bool target=false;
    if(f.attack_ticks) target=demake_general_has_hit_target(game,side,scan);
    else if(ready) {
        if(f.move_command8 && !contact) scan=direction;
        else scan=game.roll(2)?1:-1;
        target=demake_general_has_hit_target(game,side,scan);
        if(!target && !(f.move_command8 && !contact)) {
            scan=-scan;target=demake_general_has_hit_target(game,side,scan);
        }
    }
    if(!f.attack_ticks && target && ready) {
        f.attack_direction=int8_t(scan);
        f.ai_action_flags|=fighter_attack_cooldown_flag;
        f.ai_action_flags&=~fighter_mighty_hit_flag;
        if(scan==direction && game.roll(100)>=75) f.ai_action_flags|=fighter_mighty_hit_flag;
        f.attack_ticks=original::commander_attack_animation_updates;
        f.action_ticks=uint16_t(original::commander_attack_cycle_updates(game.strength(f.general)));
    } else if(f.attack_ticks==6) demake_general_apply_hit(game,side,scan);
}

#if defined(__arm__)
__attribute__((section(".iwram")))
#endif
void Game::battle_tick() {
    if(campaign.phase != Phase::battle || battle.paused || battle.row_setup_active) return;
#if defined(__arm__) && defined(SANGO_PROFILE_BATTLE_STEPS)
    int profile_start=bn::core::current_cpu_ticks();
    auto profile_stage=[&](int slot) {
        const int now=bn::core::current_cpu_ticks();
        sg_probe[slot]=maximum(uint32_t(sg_probe[slot]),uint32_t(now-profile_start));
        profile_start=now;
    };
#else
    auto profile_stage=[](int) {};
#endif
    if(battle.pending_winner >= 0) {
        if(battle.effect_ticks) --battle.effect_ticks;
        else resolve_duel(battle.pending_winner);
        return;
    }
    ++battle.tick;
    ++battle.duel_tick;
    const int hp_before[2]={battle.sides[0].hp,battle.sides[1].hp};
    for(auto& ticks : battle.general_hit_ticks) if(ticks) --ticks;
    for(auto& f : battle.sides) f.previous_x = f.x;
    for(auto& s : battle.soldiers) { s.previous_x = s.x; s.previous_y = s.y; }
    cancel_retreat_steps(battle);
    // Native FUN_0043d0b3 interpolates one cell move over 0x20 actor updates.
    // Keep logical target cells reserved while the GBA sprite travels there.
    // Formation commands can queue a move during an attack. demake finishes
    // its attack animation/event before entering the movement state.
    for(auto& s : battle.soldiers) if(s.hp > 0 && s.move_ticks && !s.attack_ticks) {
        int x_remainder = int(s.move_x_remainder) + s.move_dx * battle_grid_column_width;
        while(x_remainder >= 32) { ++s.x; x_remainder -= 32; }
        while(x_remainder <= -32) { --s.x; x_remainder += 32; }
        s.move_x_remainder = int8_t(x_remainder);
        s.move_y_remainder = int8_t(s.move_y_remainder + s.move_dy * battle_grid_row_height);
        if(s.move_y_remainder >= 32) { ++s.y; s.move_y_remainder = int8_t(s.move_y_remainder - 32); }
        else if(s.move_y_remainder <= -32) { --s.y; s.move_y_remainder = int8_t(s.move_y_remainder + 32); }
        if(!--s.move_ticks) {
            s.x = s.move_target_x;
            s.y = s.move_target_y;
            s.move_dx = s.move_dy = 0;
            s.move_x_remainder = s.move_y_remainder = 0;
        }
    }
    if(battle.effect_ticks == 1) apply_pending_effect();
    if(battle.effect_ticks) --battle.effect_ticks;
    if(battle.effect_remaining) --battle.effect_remaining;
    // demake's 2663/2664 wrappers instantiate their child prefabs on a
    // wall-clock schedule, so the GBA keeps a small supervisor separate from
    // the active child's normal effect timer. Existing child implementations
    // remain responsible for their own collisions and visuals.
    if(battle.magic_composite_kind) {
        if(battle.magic_composite_bootstrap) {
            battle.magic_composite_bootstrap=0;
            battle.magic_composite_stage=0;
            start_magic_composite_child();
            if(battle.magic_composite_kind==1) {
                battle.magic_composite_stage=1;
                battle.magic_composite_wait=45; // demake 1.5 seconds.
            } else {
                battle.magic_composite_stage=1;
                battle.magic_composite_bootstrap=1;
                start_magic_composite_child(); // 2604 and 2622 start together.
                battle.magic_composite_bootstrap=0;
                battle.magic_composite_stage=2;
                battle.magic_composite_wait=30; // demake 1 second.
            }
        } else if(battle.magic_composite_wait) {
            --battle.magic_composite_wait;
        } else if(battle.magic_composite_stage<3) {
            start_magic_composite_child();
            ++battle.magic_composite_stage;
            battle.magic_composite_wait = battle.magic_composite_kind==1 &&
                battle.magic_composite_stage==2 ? 30 : 0;
        } else if(!battle.effect_remaining) {
            battle.magic_composite_kind=0;
            battle.magic_composite_stage=0;
        }
    }
    if((battle.effect_script==2627 || battle.effect_script==2628) &&
       battle.magic_field_state==0 && battle.effect_remaining &&
       original::magic_busy_duration(battle.effect_script)-battle.effect_remaining>=50) {
        // MAGIC01 4000 samples two random enemy handles per loop, repeats
        // floor(live_count/2)+1 times, and keeps the extreme x for this side.
        // The selected coordinate drives both native256 camera focus and the
        // later GLOBAL2508 spawn point.
        int live_indices[200];
        int live_count=0;
        for(int i=0;i<200;++i)
            if(battle.soldiers[(1-battle.effect_side)*200+i].hp>0)
                live_indices[live_count++]=i;
        int selected_x=battle.sides[battle.effect_side].x;
        bool has_live_target=live_count!=0;
        if(live_count) {
            const int sample_pairs=live_count/2+1;
            bool selected=false;
            for(int sample=0;sample<sample_pairs;++sample) {
                const auto& first=battle.soldiers[(1-battle.effect_side)*200+
                                                   live_indices[roll(live_count)]];
                const auto& second=battle.soldiers[(1-battle.effect_side)*200+
                                                    live_indices[roll(live_count)]];
                if(!selected || (battle.effect_side==0 ? second.x>first.x : second.x<first.x))
                    selected_x=second.x;
                else selected_x=first.x;
                selected=true;
            }
        } else {
            // MAGIC01's empty-roster branch offsets the spawn anchor by 320
            // native pixels; the following camera offset cancels that shift.
            selected_x+=battle.effect_side==0 ?
                320*battle_grid_column_width/96 : -320*battle_grid_column_width/96;
        }
        battle.magic_field_center_x=int16_t(selected_x);
        battle.magic_field_state=has_live_target ? 1 : 2;
    }
    if(battle.heal_visual_remaining) --battle.heal_visual_remaining;
    if(battle.heal_wait) --battle.heal_wait;
    if(battle.heal_remainder_wait) --battle.heal_remainder_wait;
    if(!battle.heal_wait && battle.heal_steps) {
        auto& healer = battle.sides[battle.effect_side];
        healer.hp = int16_t(minimum(max_hp(healer.general),int(healer.hp)+1));
        --battle.heal_steps;
        // GLOBAL6300 continues its 60-update loop after its first15 +1 heals;
        // the remaining amount is applied only after that loop exits.
        if(!battle.heal_steps && battle.heal_remainder) battle.heal_remainder_wait=46;
    } else if(!battle.heal_steps && battle.heal_remainder &&
              !battle.heal_remainder_wait) {
        auto& healer = battle.sides[battle.effect_side];
        healer.hp = int16_t(minimum(max_hp(healer.general),
                                    int(healer.hp)+battle.heal_remainder));
        battle.heal_remainder = 0;
    }
    for(auto& ticks : battle.immunity_ticks) if(ticks) --ticks;
    // Preserve actor order, but skip unused slots in every targeting query.
    // Effects can add soldiers before this point; afterwards actors only die.
    profile_stage(121);
    uint16_t live[400];
    int live_count = 0, begin[3];
    for(int side=0; side<2; ++side) {
        begin[side] = live_count;
        for(int i=0; i<200; ++i) {
            const int index = side*200+i;
            if(battle.soldiers[index].hp > 0) live[live_count++] = uint16_t(index);
        }
    }
    begin[2] = live_count;
    bool movement_index_ready=false;

    constexpr int grid_origin_x = battlefield_width / 2 - 28 * battle_grid_column_width;
    auto grid_column = [](int x) {
        const int offset = x - grid_origin_x + battle_grid_column_width/2;
        return offset >= 0 ? offset / battle_grid_column_width : -1;
    };
    auto grid_row = [](int y) {
        const int offset = y + battle_grid_row_height / 2;
        return offset >= 0 ? offset / battle_grid_row_height : -1;
    };

    // Projectiles run before the actors move. Build an occupancy index once,
    // retaining the original slot order when multiple actors share a cell.
    constexpr int indexed_columns=battle_grid_columns+1;
    int16_t cell_heads[indexed_columns*battle_grid_rows], cell_next[400];
    for(auto& head : cell_heads) head=-1;
    for(int n=live_count-1; n>=0; --n) {
        const int index=live[n];
        const auto& soldier=battle.soldiers[index];
        const int column=grid_column(soldier.x), row=grid_row(soldier.y);
        cell_next[index]=-1;
        if(unsigned(column)<indexed_columns && unsigned(row)<battle_grid_rows) {
            const int cell=row*indexed_columns+column;
            cell_next[index]=cell_heads[cell]; cell_heads[cell]=int16_t(index);
        }
    }
    auto cell_head = [&](int column,int row) {
        return unsigned(column)<indexed_columns && unsigned(row)<battle_grid_rows ?
            int(cell_heads[row*indexed_columns+column]) : -1;
    };

    auto relocate_soldier = [&](int index,int old_x) {
        const auto& soldier=battle.soldiers[index];
        const int old_column=grid_column(old_x), column=grid_column(soldier.x), row=grid_row(soldier.y);
        if(old_column==column || unsigned(row)>=battle_grid_rows) return;
        if(unsigned(old_column)<indexed_columns) {
            auto* link=&cell_heads[row*indexed_columns+old_column];
            while(*link>=0 && *link!=index) link=&cell_next[*link];
            if(*link==index) *link=cell_next[index];
        }
        cell_next[index]=-1;
        if(unsigned(column)<indexed_columns) {
            auto* link=&cell_heads[row*indexed_columns+column];
            while(*link>=0 && *link<index) link=&cell_next[*link];
            cell_next[index]=*link; *link=int16_t(index);
        }
    };

    update_magic_field();
    update_battle_projectiles(cell_heads,cell_next);
    profile_stage(122);
    for(int side = 0; side < 2; ++side) {
        auto& f = battle.sides[side];
        auto& enemy = battle.sides[1 - side];
        if(f.attack_ticks) --f.attack_ticks;
        if(f.action_ticks) --f.action_ticks;
        bool action_ready = f.action_ticks == 0;
        if(action_ready) {
            f.ai_action_flags&=~fighter_attack_cooldown_flag;
            battle.set_magic_delay_active(side,false);
        }
        if(action_ready && f.action_delay) {
            f.action_ticks = f.action_delay;
            f.action_delay = 0;
            battle.set_magic_delay_active(side,true);
            action_ready = false;
        }
        if(action_ready) {
            int interval = original::battle_action_interval;
            if(f.move_command8) {
                // Native FUN_0043c821 applies the grade differential only
                // to the general actor (flag bit0) after command8 movement.
                // FUN_004428e6 maps to the officer's equipped item and skill
                // slots, which are represented directly in the GBA record.
                const auto& own = campaign.officers[f.general];
                const auto& opposing = campaign.officers[enemy.general];
                const int own_grade = original::battle_officer_grade(
                    own.equipped_item,own.learned[1],own.learned[2],own.learned[3]);
                const int opposing_grade = original::battle_officer_grade(
                    opposing.equipped_item,opposing.learned[1],opposing.learned[2],opposing.learned[3]);
                interval = original::commander_moving_action_interval(own_grade,opposing_grade);
            }
            f.action_ticks = uint16_t(interval);
        }
        // demake WSInfoPanel grows the sword meter from STR alone at
        // (STR / 30 + 3) pixels per second. Convert that to this 30 Hz loop.
        if(f.hp>0 && f.charge < original::cast_charge_max)
            f.charge = uint32_t(minimum(original::cast_charge_max,int(f.charge)+
                original::battle_charge_increment(strength(f.general))));
        int direction = side ? 1 : -1;
        const bool controlled = campaign.officers[f.general].king == campaign.player;
        if(!controlled) auto_cast_skill(side);
        const bool casting=(battle.effect_remaining && battle.effect_side==side) ||
            (battle.magic_composite_kind && battle.magic_composite_side==side);
        constexpr int commander_lane_y = battle_commander_y;
        // demake checks the next allied cell in the current movement direction,
        // including the rear path during Escape. Target acquisition ignores
        // allies, so it cannot also serve as movement collision.
        const bool melee_contact=commander_lane_blocked(battle,cell_heads,cell_next,side,f.order==Order::retreat);
        if(melee_contact) f.move_command8=0;
        if(!casting && f.order == Order::retreat && f.hp>0) {
            if(!melee_contact) f.x = int16_t(f.x-direction*original::commander_retreat_step(battle.tick));
            // Front deployment changes the starting point, not the exit.
            // A displacement from deployment can still be inside the field.
            if((f.x <= 0 || f.x >= battlefield_width-1) && enemy.hp>0) {
                const int moved_x=int(f.x)-f.previous_x;
                if(moved_x) f.facing=uint8_t(moved_x>0);
                auto& visual=battle.general_visual[side];
                if(moved_x || visual.motion_hold) update_actor_visual(visual,moved_x,0,false);
                f.ai_action_flags|=fighter_escaped_flag;
                battle.pending_winner = int8_t(1 - side); battle.effect_ticks = 30; return;
            }
        } else if(!casting && f.hp>0 && (f.order == Order::advance || f.order == Order::general ||
                   (!controlled && f.soldiers == 0 &&
                    (f.ai_action_flags&fighter_opening_finished_flag))) && !melee_contact) {
            int target_x = f.x, closest = battlefield_width;
            const int commander_column = grid_column(f.x);
            const int commander_row = grid_row(commander_lane_y);
            for(int col=commander_column+direction; unsigned(col)<battle_grid_columns; col+=direction) {
                for(int index=cell_head(col,commander_row); index>=0; index=cell_next[index]) {
                    const auto& candidate=battle.soldiers[index];
                    if(candidate.hp<=0 || (index>=200)==(side!=0)) continue;
                    const int distance=(int(candidate.x)-f.x)*direction;
                    if(distance<closest) {
                        closest=distance; target_x=candidate.x;
                    }
                }
                if(closest<battlefield_width) break;
            }
            // General attacks query the forward cell in the center lane; a
            // nearby soldier in another row cannot be hit and must not pin
            // the commander in place. Keep pursuit based on the lane query
            // above so the movement target agrees with FUN_0043e678.
            const int general_distance = (int(enemy.x) - f.x) * direction;
            const int enemy_column = grid_column(enemy.x);
            const int enemy_cell_ahead = (enemy_column-commander_column)*direction;
            if(enemy_column >= 0 && enemy_column < battle_grid_columns &&
               enemy_cell_ahead > 0 && general_distance < closest) {
                closest = general_distance;
                target_x = enemy.x;
            }
            // Native command8 resolves an occupied destination through the
            // single-unit battle grid. Keep the commander in the adjacent
            // one-cell gap instead of walking into a general's occupied cell.
            if(battle.tick % 3 == 0 && closest < battlefield_width && closest > battle_grid_column_width*9/10) {
                f.facing = uint8_t(target_x > f.x);
                // Sprite weapon reach and physical contact use one cell.
                // The old two-cell stand-off produced visible air swings.
                const int minimum_gap = battle_grid_column_width;
                if(closest > minimum_gap)
                    f.x = int16_t(clamp(int(f.x) + direction*minimum(battle_grid_column_width/20,closest-minimum_gap),0,battlefield_width-1));
                f.move_command8 = 1;
            }
        }
        const int moved_x=int(f.x)-f.previous_x;
        if(moved_x) f.facing=uint8_t(moved_x>0);
        auto& commander_visual=battle.general_visual[side];
        if(moved_x || commander_visual.motion_hold) update_actor_visual(commander_visual,moved_x,0,false);
        // Only two commanders: keep their decision scheduler in ROM and
        // reserve fast RAM for the many-soldier simulation and audio stack.
        demake_general_ai_tick(*this,side,controlled);
        profile_stage(123);
        for(int n=begin[side]; n<begin[side+1]; ++n) {
            auto& s = battle.soldiers[live[n]];
            if(s.hp <= 0) continue;
            // Main-general order sends the mounted commander forward while
            // the ranks hold their ground to keep the two actors distinct.
            const int tactical_command = s.formation_column < 8 ? f.ai_command_slots[s.formation_column] : 0;
            const bool front_rank=soldier_in_front_rank(s);
            bool advance = f.order == Order::advance ||
                (f.order == Order::front && front_rank) || (f.order == Order::rear && !front_rank);
            const bool front_back=f.order==Order::front_back && front_rank;
            if(tactical_command == 1) advance = true;
            if((f.order == Order::retreat || front_back || tactical_command == 8) && battle.tick % 3 == 0) {
                const int old_x=s.x;
                s.x = int16_t(clamp(int(s.x) - direction*(battle_grid_column_width/20), 0, battlefield_width - 1));
                relocate_soldier(live[n],old_x);
                if(movement_index_ready && !s.move_ticks) movement_occupancy.relocate(live[n],s.x,s.y);
            }
            auto& visual=battle.soldier_visual[live[n]];
            const int actor_moved_x=int(s.x)-s.previous_x, actor_moved_y=int(s.y)-s.previous_y;
            if(actor_moved_x || actor_moved_y || s.move_ticks || visual.motion_hold)
                update_actor_visual(visual,actor_moved_x,actor_moved_y,s.move_ticks > 0);
            const bool ranged = is_ranged_force(f.force);
            if(s.action_ticks) --s.action_ticks;
            bool soldier_action_ready = s.action_ticks == 0;
            if(soldier_action_ready) battle.set_magic_delay_active(2+live[n],false);
            if(soldier_action_ready && s.action_delay) {
                s.action_ticks = s.action_delay;
                s.action_delay = 0;
                battle.set_magic_delay_active(2+live[n],true);
                soldier_action_ready = false;
            }
            if(soldier_action_ready) s.action_ticks = uint16_t(s.move_command8 ? 37 : original::battle_action_interval);
            const bool soldier_attack_event = s.attack_ticks == 1;
            if(s.attack_ticks) --s.attack_ticks;
            const bool retreating=f.order==Order::retreat || front_back || tactical_command==8;
            int nearest=-1;
            int pursuit_target=-1;
            int lane_direction=s.attack_direction ? s.attack_direction : direction;
            bool general_in_lane_range=false;
            if(!retreating && (soldier_attack_event || (soldier_action_ready && !s.move_ticks && !s.attack_ticks))) {
                if(!movement_index_ready) { movement_occupancy.reset(battle.soldiers); movement_index_ready=true; }
                if(advance) pursuit_target=movement_occupancy.pursuit_target(live[n],side,battle.sides);
                if(!soldier_attack_event) lane_direction=roll(2) ? 1 : -1;
                auto enemy_in_direction = [&]() {
                    // demake melee checks four 12-unit occupancy cells in either
                    // direction, including the actor's cell. Quantize GBA x to
                    // demake's 2200-unit field before comparing those cells.
                    if(ranged) {
                        // demake's ranged acquisition reaches 1000 world units.
                        // One GBA occupancy column maps to about 39.3 demake units.
                        const int target=movement_occupancy.lane_target(live[n],lane_direction,25,battle.sides);
                        if(target>=0 && target<400 && target/200==1-side) { nearest=target%200; return true; }
                        if(target==401-side) { general_in_lane_range=true; return true; }
                    } else {
                        const int row=(int(s.y)+battle_grid_row_height/2)/battle_grid_row_height;
#if defined(SANGO_REFERENCE_MELEE_SCAN)
                        // Host differential oracle: original full-slot query.
                        for(int target=(1-side)*200;target<(2-side)*200;++target) {
                            const auto& candidate=battle.soldiers[target];
                            const int target_row=(int(candidate.y)+battle_grid_row_height/2)/battle_grid_row_height;
                            if(candidate.hp>0 && target_row==row && demake_general_hit_range(s.x,candidate.x,lane_direction)) {
                                nearest=target%200;
                                return true;
                            }
                        }
#else
                        // demake's inclusive four-cell reach is at most 61 GBA
                        // world units after quantization. The existing current-
                        // position grid covers that with two columns each way.
                        // Keep the lowest original slot, irrespective of cell
                        // traversal order, so targeting and RNG stay unchanged.
                        const int column=grid_column(s.x);
                        int first=400;
                        for(int col=maximum(0,column-2);col<=minimum(indexed_columns-1,column+2);++col)
                            for(int target=cell_head(col,row);target>=0;target=cell_next[target]) {
                                const auto& candidate=battle.soldiers[target];
                                if(target<first && target/200==1-side && candidate.hp>0 &&
                                   demake_general_hit_range(s.x,candidate.x,lane_direction)) first=target;
                            }
                        if(first<400) { nearest=first%200; return true; }
#endif
                    }
                    if(enemy.soldiers==0 && (!advance || pursuit_target>=400) &&
                       demake_general_hit_range(s.x,enemy.x,lane_direction) &&
                       (int(s.y)+battle_grid_row_height/2)/battle_grid_row_height==
                           (battle_commander_y+battle_grid_row_height/2)/battle_grid_row_height) {
                        general_in_lane_range=true;
                        return true;
                    }
                    return false;
                };
                if(!enemy_in_direction() && !soldier_attack_event) {
                    lane_direction=-lane_direction;
                    enemy_in_direction();
                }
            }
            const bool has_attack_target=nearest>=0 || general_in_lane_range;
            if(soldier_action_ready && !s.move_ticks && !s.attack_ticks && has_attack_target &&
               (!ranged || ranged_attack_trigger(*this,s,
                    nearest>=0 ? battle.soldiers[(1-side)*200+nearest].x : enemy.x,f.force))) {
                s.attack_direction=int8_t(lane_direction);
                visual.facing=uint8_t(lane_direction>0);
                s.move_command8 = 0;
                // Native deployment selects M/W and front/rear object IDs.
                // Their Things.ini Attack sequences place OM_ATTACK at
                // different ages for M5 and M6; retain per-unit timing.
                s.attack_ticks = uint8_t(original::soldier_attack_event_delay(f.force,side,s.rear));
            }
            // Casting immunity belongs to the enemy general. It must not
            // suspend ordinary soldier-vs-soldier attacks for that side.
            // FUN_0043c821 gives each actor its own countdown: 32 updates
            // normally, or37 after the movement helper sets actor flag0x4000.
            if(soldier_attack_event && nearest >= 0) {
                // 0x43e34b is the hit test; after a miss, rear attackers
                // retry at 2/3 threshold. 0x44397c maps object bit 4 to the
                // rear-rank count. ForceSoldierObjectProcess turns hit event
                // 0x10004 into death/removal event 0x10003 immediately.
                if(ranged) {
                    for(auto& p : battle.projectiles) if(!p.active) {
                        p={};
                        p.active = 1; p.x = s.x; p.y = s.y; p.dx = int16_t(lane_direction * (battle_grid_column_width/10));
                        p.target_offset_x=s.x; // Normal-arrow launch origin.
                        p.kind = lane_direction > 0 ? 1 : 2; p.attacker_rear = s.rear;
                        p.target_side = int8_t(1 - side); p.owner_index = s.index;
                        p.collision_enabled = 1; p.remaining = 0; break;
                    }
                } else demake_soldier_apply_hit(*this,side,lane_direction,s.x,s.y);
            }
            if(soldier_attack_event && !battle.immunity_ticks[1-side] &&
               general_in_lane_range) {
                if(ranged) {
                    for(auto& p : battle.projectiles) if(!p.active) {
                        p={};
                        p.active = 1; p.x = s.x; p.y = s.y; p.dx = int16_t(lane_direction * (battle_grid_column_width/10));
                        p.target_offset_x=s.x;
                        p.kind = lane_direction > 0 ? 1 : 2; p.attacker_rear = s.rear;
                        p.target_side = int8_t(1 - side); p.owner_index = s.index;
                        p.collision_enabled = 1; p.remaining = 0; break;
                    }
                } else demake_soldier_damage_general(*this,side);
            }
            // Acquire an enemy before advancing, and let the attack sequence
            // finish before issuing the next movement step.
            if(soldier_action_ready && advance && !retreating && !has_attack_target && !s.move_ticks && !s.attack_ticks) {
                s.move_command8 = 1;
                movement_occupancy.pursue(s,pursuit_target,battle.sides,*this);
            }
        }
        // Native type-4 ATTACK sends OM_ATTACK on update 15 of a 21-update
        // sequence. demake GeneralController uses a 48-unit / four-cell reach;
        // its hit callback applies to every occupied enemy cell in that lane.
        update_commander_attack(*this,side,direction,action_ready,melee_contact,casting);
    }
    profile_stage(124);
    for(int side=0;side<2;++side) if(battle.sides[side].hp<hp_before[side]) {
        battle.general_hit_ticks[side]=18;
        battle.general_hit_damage[side]=uint16_t(hp_before[side]-battle.sides[side].hp);
    }
    // Entering the native combat controller resets its timer for each pair.
    if(battle.sides[0].hp <= 0 || battle.sides[1].hp <= 0 || battle.duel_tick >= original::battle_timeout_updates) {
        int winner = battle.sides[0].hp <= 0 && battle.sides[1].hp <= 0 ? 2 :
            battle.sides[0].hp <= 0 ? 1 : battle.sides[1].hp <= 0 ? 0 :
            2;
        battle.pending_winner = int8_t(winner);
        battle.effect_ticks = 45;
        notice = winner == 2 ? "本阵时间到，双方平局" : "本阵战斗结束";
    }
}

}
