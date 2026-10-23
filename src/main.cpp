#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_bg_palettes.h"
#include "render.h"
#include "save.h"
#include "audio.h"
#include "original_rules.h"
#include "ui_navigation.h"
#include "city_navigation.h"
#include "battle_input.h"
#include "save_slot_ui.h"

// Read-only emulator telemetry, also useful when inspecting the ELF in a debugger.
extern "C" {
BN_DATA_EWRAM volatile uint32_t sg_probe[136] = {};
}

namespace {
enum class Screen { title, kings, map, city, officers, destination, assemble, pause, title_confirm, battle, commands, prisoners, ended, troops, promotion, major, commander, items, battle_setup, battle_report, save_slots, save_overwrite, game_over, victory_epilogue, victory_credits, help, find_generals, find_general_confirm, power_map, power_detail, army_list, army_detail, army_commands, army_target, army_prisoners, city_prisoners, city_prisoner_detail, prisoner_persuasion, domestic_generals, battle_round_result, battle_general_info, battle_retreat_confirm, expedition_abandon_confirm, expedition_prefect, expedition_funds, domestic_finish_confirm, troop_types, domestic_result, king_confirm, army_prisoner_detail, army_general_info, major_result, load_confirm, prisoner_release_confirm, battle_help, year_end_dialogue };
BN_DATA_EWRAM sg::Game game;
Screen screen = Screen::title;
int scenario = 1, king = 1, city = 5, cursor = 0, officer_cursor = 0;
int title_cursor = 0;
bool audio_options=false;
int audio_cursor=0;
int slot_cursor = 0;
int find_general_count = 0, find_selected_general = -1;
int power_forces[sg::max_kings], power_force_count = 0, power_cursor = 0, power_city_cursor = 0;
int army_indices[sg::max_armies], army_count = 0, army_cursor = 0;
int map_army_focus=-1;
int army_target_count=0, army_target_cursor=0;
int army_prisoner_indices[sg::general_count], army_prisoner_count=0, army_prisoner_cursor=0;
int army_prisoner_info_section=0, army_general_cursor=0;
int city_prisoner_cursor=0, prisoner_cursor=0;
int domestic_roster_count=0, domestic_roster_cursor=0, domestic_action=2;
bool domestic_from_pause=false;
int domestic_result_worker=-1, domestic_result_city=-1, domestic_result_item=-1;
uint32_t domestic_result_delta=0;
int troop_type_list[sg::unit_types], troop_type_count=0, troop_type_cursor=0;
int expedition_prefect_candidates[sg::general_count], expedition_prefect_count=0;
int expedition_funds=0;
Screen expedition_abandon_return=Screen::assemble;
bool slot_action_save = false;
bool slot_page_from_pause = false;
bool battle_speed_up = false;
bool battle_fit_all = false;
sg::ui::BattleShoulders battle_shoulders;
sg::ui::ShoulderGesture map_shoulders;
// Transient in-battle feedback for a rejected skill cast. Kept out of the save
// model: it is pure presentation and must not survive a load.
const char* battle_notice = nullptr;
int battle_notice_ticks = 0;
int release_general=-1;
struct SlotSummary { uint8_t valid,scenario,player,cities,generals,month; uint16_t year; uint32_t sequence; };
SlotSummary slot_summaries[8]{};
uint8_t slot_checked_mask=0;
bool slot_page_presented=false;
int destination = -1, skill_slot = 0, item_cursor = 0;
int group[5], group_count = 0;
bool world_paused = true;
// Strategic-clock multiplier. A campaign runs ~90 in-game years, and the
// original advances one 300-tick month in about ten seconds, so watching the
// map at 1x is by far the longest passive stretch of the game. Default 1x keeps
// the original pacing; the world pause menu lets the player raise it.
int map_speed = 1;
bool map_zoomed = false;
bool title_scenarios = false;
const char* title_message = nullptr;
bool officer_detail = false;
bool battle_setup_actions = false;
int battle_setup_action_cursor = 0;
int battle_info_side = 0, battle_info_cursor = 0;
bool items_global_roster = false;
bool officers_global_roster = false;
bool promotion_global_roster = false;
BN_DATA_EWRAM char promotion_success_notice[48];
bool redraw = true;
uint32_t frames = 0;
uint32_t ending_frames = 0;
const char* observed_notice = nullptr;
const char* map_message = nullptr;
int map_notice_frames = 0;
BN_DATA_EWRAM int officers[sg::general_count];
BN_DATA_EWRAM int domestic_generals[sg::general_count];
BN_DATA_EWRAM int destinations[sg::city_count];
BN_DATA_EWRAM int prisoners[sg::general_count];
bool repeat_direction(bool pressed, bool held, uint8_t& counter) {
    return sg::ui::repeat_direction(pressed,held,counter,1+bn::core::last_missed_frames());
}
uint8_t up_repeat = 0, down_repeat = 0, left_repeat = 0, right_repeat = 0;
uint8_t troop_l_repeat = 0, troop_r_repeat = 0;
const int city_action_count = 6;
const int world_city_action_count = 5;
// Item effect labels for the stat items are derived from the rule tables rather
// than written out by hand: item 19 was labelled "+8" while the model applied
// +10, and a hand-written table can always drift again. The buffers live in
// EWRAM because IWRAM has no room left before the stack reserve.
BN_DATA_EWRAM char item_stat_labels[8][20];
BN_DATA_EWRAM char strength_item_labels[19][20];
void write_bonus_label(char* out, const char* prefix, int bonus) {
    int length = 0;
    while(prefix[length]) { out[length] = prefix[length]; ++length; }
    out[length++] = '+';
    char digits[6]; int end = 6;
    int value = bonus;
    do { digits[--end] = char('0' + value % 10); value /= 10; } while(value);
    for(int d = end; d < 6; ++d) out[length++] = digits[d];
    out[length] = 0;
}
const char* item_stat_label(int item) {
    static bool built = false;
    if(!built) {
        built = true;
        for(int i = 0; i < 8; ++i) {
            const int id = 12 + i;
            if(id <= 16) write_bonus_label(item_stat_labels[i],"智力",sg::original::item_intelligence_bonus(id));
            else if(id == 17 || id == 19) write_bonus_label(item_stat_labels[i],"技力上限",sg::original::item_max_mp_bonus(id));
            else write_bonus_label(item_stat_labels[i],"体力上限",sg::original::item_max_hp_bonus(id));
        }
    }
    return item >= 12 && item <= 19 ? item_stat_labels[item-12] : nullptr;
}
const char* strength_items_label(int item) {
    static bool built = false;
    if(!built) {
        built = true;
        for(int i = 0; i < 19; ++i)
            write_bonus_label(strength_item_labels[i],"武力",sg::original::item_strength_bonus(20 + i));
    }
    return item >= 20 && item <= 38 ? strength_item_labels[item-20] : nullptr;
}
const char* equipment_name(int item) {
    static const char* names[]={"孙子兵法","孟德新书","兵书二十四篇","史记","春秋左传",
        "遁甲天书","青囊书","太平要术","青龙偃月刀","方天画戟","丈八蛇矛","倚天剑",
        "青虹剑","双铁戟","双刃斧","古锭刀","七星剑","双股剑","铁蒺黎骨朵","三尖刀",
        "铁脊蛇矛","大斧","凤嘴刀","月牙戟","流星锤","短锥枪","眉尖刀","的卢马",
        "赤兔马","爪黄飞电","大宛马"};
    return item>=12 && item<=42 ? names[item-12] : nullptr;
}
const char* helper_item_name(int item) {
    static const char* troop_names[11] = {"朴刀兵","长枪兵","大刀兵","弓箭兵","链锤兵","飞刀兵","武斗兵","蛮族兵","铁锤兵","藤甲兵","黄巾兵"};
    static const char* formation_names[7] = {"方圆阵","圆阵","锋矢阵","雁形阵","玄襄阵","鹤翼阵","鱼鳞阵"};
    if(item >= 1 && item <= 11) return troop_names[item-1];
    if(item >= 12 && item <= 42) return equipment_name(item);
    if(item >= 43 && item <= 49) return formation_names[item-43];
    if(item >= 50 && item <= 54) return "未鉴定宝物";
    return nullptr;
}
int owned_city_item_count(const sg::Campaign& campaign,int faction) {
    int count=0;
    for(int city_index=0;city_index<sg::city_count;++city_index)
        if(campaign.cities[city_index].king==faction) count+=sg::city_inventory_count(campaign,city_index);
    return count;
}
bool owned_city_item_location(const sg::Campaign& campaign,int faction,int row,int& found_city,int& item) {
    for(int current=0;current<sg::city_count;++current) {
        if(campaign.cities[current].king!=faction) continue;
        const int count=sg::city_inventory_count(campaign,current);
        if(row<count) { found_city=current; item=row; return true; }
        row-=count;
    }
    return false;
}
const char* city_object_name(uint8_t entry) {
    const int type=sg::city_inventory_type(entry),index=sg::city_inventory_index(entry);
    // Object identity is not its equipment bonus. Search stores an object in
    // the city; it does not apply that object's effects to the searching general.
    static const char* tokens[]={"朴刀兵兵符","长枪兵兵符","大刀兵兵符","弓箭兵兵符",
        "链锤兵兵符","飞刀兵兵符","武斗兵兵符","蛮族兵兵符","铁锤兵兵符","藤甲兵兵符","黄巾兵兵符"};
    static const char* books[]={"方圆阵之书","圆阵之书","锋矢阵之书","雁形阵之书","玄襄阵之书","鹤翼阵之书","鱼鳞阵之书"};
    if(type==0) return helper_item_name(index);
    if(type==1 && index<31) return equipment_name(12+index);
    if(type==2 && index<11) return tokens[index];
    if(type==3 && index<7) return books[index];
    return "\u672a\u77e5\u7269\u54c1";
}
const char* orders[] = {"待命", "全军突击", "前军前进", "后军前进", "主将出击", "撤退", "前军后撤"};
const char* battle_commands[] = {"前军前进", "前军后撤", "兵散开", "整队", "待命", "全军突击", "主将出击", "撤退"};
int pause_action_count() { return game.campaign.phase == sg::Phase::domestic ? 15 : 8; }
int pause_columns() { return game.campaign.phase == sg::Phase::domestic ? 3 : 2; }
const char* pause_action(int index) {
    static const char* domestic_actions[] = {
        "使用物品", "武将资料", "劝降俘虏", "搜索", "筑城防御", "开发人口", "势力分布", "武将晋升",
        "结束内政", "保存", "回到标题", "继续", "读取", "武将定位", "操作说明"
    };
    if(game.campaign.phase == sg::Phase::domestic) return domestic_actions[sg::clamp(index,0,14)];
    static const char* world_actions[] = {"继续", "保存", "读取", "武将定位", "势力分布", "操作说明", "回到标题"};
    if(index == 7) {
        static const char* speed_actions[] = {"地图速度x1", "地图速度x2", "地图速度x4"};
        return speed_actions[map_speed >= 4 ? 2 : map_speed >= 2 ? 1 : 0];
    }
    return world_actions[sg::clamp(index,0,6)];
}

void open_find_generals() {
    find_general_count=0; officer_cursor=0; find_selected_general=-1;
    // The original locator pins the faction ruler to the top of the list,
    // independent of the ruler's general ID in the officer table.
    const int ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
    if(ruler>=0 && ruler<sg::general_count)
        officers[find_general_count++]=ruler;
    for(int general=0;general<sg::general_count;++general) {
        if(general==ruler) continue;
        const auto& officer=game.campaign.officers[general];
        if(officer.status==sg::Status::employed && officer.king==game.campaign.player)
            officers[find_general_count++]=general;
    }
    screen=Screen::find_generals;
}

void open_power_map() {
    power_force_count=0; power_cursor=0; power_city_cursor=0;
    for(int force=0;force<sg::scenario_defs[game.campaign.scenario].king_count;++force) {
        bool active=false;
        for(const auto& owned:game.campaign.cities) active|=owned.king==force;
        for(const auto& army:game.campaign.armies) active|=army.active && army.king==force;
        if(active) power_forces[power_force_count++]=force;
    }
    screen=Screen::power_map;
}

int force_city_count(int force) {
    int count=0;
    for(const auto& owned:game.campaign.cities) count+=owned.king==force;
    return count;
}
int force_general_count(int force) {
    int count=0;
    for(const auto& officer:game.campaign.officers)
        count+=officer.status==sg::Status::employed && officer.king==force;
    return count;
}
int force_city_at(int force,int row) {
    for(int current=0;current<sg::city_count;++current) if(game.campaign.cities[current].king==force) {
        if(row==0) return current;
        --row;
    }
    return -1;
}
int general_map_city(int general) {
    const auto& officer=game.campaign.officers[general];
    if(officer.city>=0 && officer.city<sg::city_count) return officer.city;
    if(officer.army>=0 && officer.army<sg::max_armies) {
        const auto& army=game.campaign.armies[officer.army];
        if(army.active && army.target>=0 && army.target<sg::city_count) return army.target;
        if(army.active && army.origin>=0 && army.origin<sg::city_count) return army.origin;
    }
    for(const auto& army:game.campaign.armies) if(army.active)
        for(int i=0;i<army.count;++i) if(army.generals[i]==general) {
            if(army.target>=0 && army.target<sg::city_count) return army.target;
            if(army.origin>=0 && army.origin<sg::city_count) return army.origin;
        }
    return -1;
}

void open_army_list() {
    game.notice="";
    army_count=0; army_cursor=0;
    for(int index=0;index<sg::max_armies;++index)
        if(game.campaign.armies[index].active) army_indices[army_count++]=index;
    if(army_count==1) {
        cursor=0;
        screen=Screen::army_commands;
    } else screen=Screen::army_list;
}
void locate_map_army(int index) {
    map_army_focus=index;
    const auto p=sg::army_position(game.campaign,index);
    int best=0x7fffffff;
    for(int c=0;c<sg::city_count;++c) {
        const int dx=p.x-sg::city_defs[c].x,dy=p.y-sg::city_defs[c].y;
        const int score=dx*dx+dy*dy;
        if(score<best) { best=score;city=c; }
    }
    screen=Screen::map; redraw=true;
}
void move_map_army(int dx,int dy) {
    const auto p=sg::army_position(game.campaign,map_army_focus);
    int best=0x7fffffff,chosen=-1;
    for(int i=0;i<sg::max_armies;++i) {
        const auto& army=game.campaign.armies[i];
        if(!army.active || i==map_army_focus) continue;
        const auto q=sg::army_position(game.campaign,i);
        const int x=q.x-p.x,y=q.y-p.y;
        if(x*dx+y*dy<=0) continue;
        const int cross=x*dy-y*dx;
        const int score=x*x+y*y+2*cross*cross;
        if(score<best) { best=score;chosen=i; }
    }
    if(chosen>=0) locate_map_army(chosen);
}
int count_army_prisoners(int army) {
    int count=0;
    for(const auto& officer:game.campaign.officers)
        count+=officer.status==sg::Status::prisoner && officer.army==army && officer.city<0;
    return count;
}
void open_army_prisoners(int army) {
    army_prisoner_count=0; army_prisoner_cursor=0;
    for(int general=0;general<sg::general_count;++general) {
        const auto& officer=game.campaign.officers[general];
        if(officer.status==sg::Status::prisoner && officer.army==army && officer.city<0)
            army_prisoner_indices[army_prisoner_count++]=general;
    }
    screen=Screen::army_prisoners;
}

void step_slot_summaries() {
    if(screen!=Screen::save_slots || !slot_page_presented || slot_checked_mask==255) return;
    // Prioritize the visible selection. At most one complete slot is checked
    // between input polls; never skip integrity or legacy migration checks.
    int i=slot_cursor;
    if(slot_checked_mask&(1<<i)) {
        i=0;while(slot_checked_mask&(1<<i)) ++i;
    }
    auto& slot=slot_summaries[i];slot={};
    slot.valid=sg::save_slot_info(i,slot.scenario,slot.player,slot.cities,slot.generals,
                                 slot.year,slot.month,slot.sequence);
    slot_checked_mask|=uint8_t(1<<i);
    redraw=true;
}

void open_slot_page(bool save,bool from_pause) {
    slot_cursor=0; slot_action_save=save; slot_page_from_pause=from_pause;
    game.notice=""; slot_checked_mask=0; slot_page_presented=false;
    for(auto& slot:slot_summaries) slot={};
    screen=Screen::save_slots;
}

int officer_count() {
    const int force=game.campaign.cities[city].king;
    const int count=game.officers_at(city,force,officers,sg::general_count);
    // demake's city roster pins the active faction ruler to the first row.
    // Keep this presentation-only ordering out of domestic selection and AI.
    if(game.campaign.phase==sg::Phase::world && force==game.campaign.player) {
        const int ruler=sg::king_defs[game.campaign.scenario][force].general;
        for(int i=0;i<count;++i) if(officers[i]==ruler) {
            for(int j=i;j>0;--j) officers[j]=officers[j-1];
            officers[0]=ruler;
            break;
        }
    }
    return count;
}
int collect_troop_types(int general) {
    troop_type_count=0;
    if(general<0 || general>=sg::general_count) return 0;
    const auto& officer=game.campaign.officers[general];
    for(int force=0;force<sg::unit_types;++force)
        if((officer.forces>>force)&1) troop_type_list[troop_type_count++]=force;
    return troop_type_count;
}
int force_officer_count() {
    int count=0;
    const int ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
    if(ruler>=0 && ruler<sg::general_count) {
        const auto& officer=game.campaign.officers[ruler];
        if(officer.status==sg::Status::employed && officer.king==game.campaign.player)
            officers[count++]=ruler;
    }
    for(int general=0;general<sg::general_count;++general) {
        if(general==ruler) continue;
        const auto& officer=game.campaign.officers[general];
        if(officer.status==sg::Status::employed && officer.king==game.campaign.player)
            officers[count++]=general;
    }
    return count;
}
int item_officer_count() { return items_global_roster ? force_officer_count() : officer_count(); }
int officer_roster_count() { return officers_global_roster ? force_officer_count() : officer_count(); }
void open_global_officers() {
    officers_global_roster=true;
    force_officer_count();
    officer_cursor=0; officer_detail=false; game.notice="";
    screen=Screen::officers;
}
int item_officer_city(int general) {
    const auto& officer=game.campaign.officers[general];
    if(officer.city>=0 && officer.city<sg::city_count &&
       game.campaign.cities[officer.city].king==officer.king) return officer.city;
    for(int c=0;c<sg::city_count;++c)
        if(game.campaign.cities[c].king==officer.king) return c;
    return -1;
}
void open_domestic_items() {
    items_global_roster=true;
    force_officer_count();
    officer_cursor=0; item_cursor=0;
    game.notice="";
    screen=Screen::items;
}
bool general_has_object(int general,int type,int index) {
    if(general<0 || general>=sg::general_count) return false;
    const auto& officer=game.campaign.officers[general];
    auto has_item=[&](int item) {
        if(item>=12 && item<=42 && officer.equipped_item==item) return true;
        const int force=sg::original::item_force_type(item);
        if(force>=0 && (officer.forces&(1u<<force))) return true;
        const int formation=sg::original::item_formation(item);
        if(formation>=0 && (officer.formations&(1u<<formation))) return true;
        if(item>=1 && item<=11) {
            const int flag=item-1;
            if(officer.item_flags[flag>>3]&(1u<<(flag&7))) return true;
        }
        if(item>=43 && item<=49) {
            const int flag=item-32;
            if(officer.item_flags[flag>>3]&(1u<<(flag&7))) return true;
        }
        return false;
    };
    if(type==0) return has_item(index);
    if(type==1) return officer.equipped_item==12+index;
    if(type==2) {
        const int force=sg::original::item_force_type(index+1);
        return force>=0 && (officer.forces&(1u<<force));
    }
    if(type==3) return index<7 && (officer.formations&(1u<<index));
    return false;
}
bool faction_item_owned_by_general(int general,int item) {
    if(item<0 || item>=55) return false;
    if(item>=12 && item<=42 && game.campaign.officers[general].equipped_item==item) return true;
    const int force=sg::original::item_force_type(item);
    if(force>=0 && (game.campaign.officers[general].forces&(1u<<force))) return true;
    const int formation=sg::original::item_formation(item);
    if(formation>=0 && (game.campaign.officers[general].formations&(1u<<formation))) return true;
    return false;
}
int promotion_tier(int general) {
    const int level=game.level(general);
    if(level>=10 && !game.learned_skill(general,1)) return 1;
    if(level>=20 && !game.learned_skill(general,2)) return 2;
    if(level>=40 && !game.learned_skill(general,3)) return 3;
    return 0;
}
int promotion_officer_count() {
    int count=0;
    for(int g=0;g<sg::general_count;++g) {
        const auto& officer=game.campaign.officers[g];
        if(officer.status!=sg::Status::employed || officer.king!=game.campaign.player) continue;
        if(promotion_tier(g)) officers[count++]=g;
    }
    return count;
}
bool own_selected_city() { return game.campaign.cities[city].king == game.campaign.player; }
int city_menu_count() {
    if(!own_selected_city()) return 0;
    return game.campaign.phase == sg::Phase::domestic ? city_action_count : world_city_action_count;
}
int domestic_operation_for_command(int command) { return 2-command; }
bool domestic_general_eligible(int general,int action) {
    if(general<0 || general>=sg::general_count || action<0 || action>2) return false;
    const auto& officer=game.campaign.officers[general];
    const int ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
    if(officer.status!=sg::Status::employed || officer.king!=game.campaign.player ||
       officer.city<0 || officer.army>=0) return false;
    // The ruler may work only when the faction has no other officer at all;
    // Game::domestic_ruler_only also enforces this in domestic_action.
    if(general==ruler && !game.domestic_ruler_only()) return false;
    if(game.campaign.cities[officer.city].king!=game.campaign.player) return false;
    return game.domestic_qualifies(general,action);
}
int collect_domestic_generals(int action) {
    domestic_roster_count=0;
    for(int general=0;general<sg::general_count;++general)
        if(domestic_general_eligible(general,action)) domestic_generals[domestic_roster_count++]=general;
    return domestic_roster_count;
}
bool domestic_command_available(int command) {
    const int action=domestic_operation_for_command(command);
    for(int general=0;general<sg::general_count;++general)
        if(domestic_general_eligible(general,action)) return true;
    return false;
}
bool domestic_action_available(int action) {
    for(int general=0;general<sg::general_count;++general)
        if(domestic_general_eligible(general,action)) return true;
    return false;
}
int prisoner_count() {
    int count = 0;
    for(int i = 0; i < sg::general_count; ++i) if(game.campaign.officers[i].status == sg::Status::prisoner && game.campaign.officers[i].king == game.campaign.player) prisoners[count++] = i;
    return count;
}
int city_prisoner_count(int city_index) {
    int count=0;
    if(city_index<0 || city_index>=sg::city_count) return count;
    for(int general=0;general<sg::general_count;++general) {
        const auto& officer=game.campaign.officers[general];
        if(officer.status==sg::Status::prisoner && officer.king==game.campaign.player &&
           officer.city==city_index && officer.army<0) prisoners[count++]=general;
    }
    return count;
}
void open_city_prisoners() {
    city_prisoner_count(city);
    city_prisoner_cursor=0;
    screen=Screen::city_prisoners;
}
void open_prisoner_persuasion() {
    prisoner_count();
    prisoner_cursor=0;
    game.notice="";
    screen=Screen::prisoner_persuasion;
}
bool own_city(int direction) {
    for(int i = 1; i <= sg::city_count; ++i) {
        int candidate = (city + direction*i + sg::city_count) % sg::city_count;
        if(game.campaign.cities[candidate].king == game.campaign.player) { city = candidate; officer_cursor = 0; return true; }
    }
    return false;
}
bool player_unified() {
    for(const auto& owned : game.campaign.cities)
        if(owned.king != game.campaign.player) return false;
    return true;
}
void return_to_title() {
    screen=Screen::title; title_scenarios=false; title_cursor=0; title_message=nullptr;
    game.notice=""; ending_frames=0;
}
void focus_player_city() {
    const int ruler = sg::king_defs[game.campaign.scenario][game.campaign.player].general;
    if(ruler >= 0 && ruler < sg::general_count) {
        const auto& officer = game.campaign.officers[ruler];
        if(officer.status == sg::Status::employed && officer.king == game.campaign.player &&
           officer.city >= 0 && officer.city < sg::city_count &&
           game.campaign.cities[officer.city].king == game.campaign.player) {
            city = officer.city;
            officer_cursor = 0;
            return;
        }
    }
    city = 0;
    own_city(1);
}
bool move_city(int dx, int dy) {
    const int nearest=sg::directional_city(city,dx,dy,(uint64_t(1)<<sg::city_count)-1);
    if(nearest == city) return false;
    city = nearest; officer_cursor = 0;
    return true;
}
void move_target_city(int& selected,int count,int dx,int dy) {
    if(count<=0) return;
    uint64_t allowed=0;
    for(int i=0;i<count;++i) allowed|=uint64_t(1)<<destinations[i];
    const int target=sg::directional_city(destinations[selected],dx,dy,allowed);
    for(int i=0;i<count;++i) if(destinations[i]==target) { selected=i;break; }
}
void move_city_menu(bool up, bool down, bool left, bool right) {
    int count = city_menu_count();
    if(!count) return;
    if(cursor >= count) cursor = 0;
    if(game.campaign.phase == sg::Phase::domestic) {
        // D1B_A is a single vertical command window. Keep the GBA layout
        // single-column as well so the cursor follows the original menu.
        if(up) cursor = cursor ? cursor - 1 : count - 1;
        if(down) cursor = cursor + 1 < count ? cursor + 1 : 0;
        (void)left;
        (void)right;
        return;
    }
    (void)left;
    (void)right;
    if(up) cursor = cursor ? cursor - 1 : count - 1;
    if(down) cursor = cursor + 1 < count ? cursor + 1 : 0;
}
void move_order_menu(bool up, bool down, bool left, bool right) {
    if(up) cursor = cursor >= 2 ? cursor - 2 : cursor + 6;
    if(down) cursor = cursor + 2 < 8 ? cursor + 2 : cursor % 2;
    if(left && cursor % 2) --cursor;
    if(right && !(cursor % 2)) ++cursor;
}
int player_side() {
    return game.campaign.armies[game.battle.army].king == game.campaign.player ? 0 : 1;
}
bool create_pending_expedition(const int* ordered,int count,bool abandon_city) {
    if(!game.send_army(city,city,ordered,count,false,false,expedition_funds)) return false;
    const int army_index=game.campaign.officers[ordered[0]].army;
    for(int i=0;i<count;++i) group[i]=ordered[i];
    group_count=count;
    if(abandon_city) {
        for(auto& officer:game.campaign.officers) {
            if(officer.status==sg::Status::prisoner && officer.king==game.campaign.player &&
               officer.city==city && officer.army<0) {
                officer.city=-1;
                officer.army=int16_t(army_index);
            }
        }
        game.campaign.cities[city].king=-1;
        game.campaign.cities[city].major=-1;
    }
    screen=Screen::destination;
    cursor=0;
    game.notice="";
    redraw=true;
    return true;
}
void align_phase() {
    if(screen == Screen::title || screen == Screen::kings) return;
    if(screen != Screen::battle && screen != Screen::commands && screen != Screen::battle_help) {
        battle_speed_up=false;
        battle_fit_all=false;
    }
    if(game.battle_report.pending) {
        if(screen != Screen::battle_report) { screen=Screen::battle_report; cursor=0; }
        return;
    }
    if(game.campaign.phase == sg::Phase::battle && game.battle.round_result_pending) {
        if(screen != Screen::battle_round_result) screen=Screen::battle_round_result;
        return;
    }
    if(game.campaign.phase == sg::Phase::battle && game.battle.row_setup_active &&
       screen != Screen::battle_setup && screen != Screen::battle_general_info &&
       screen != Screen::battle_retreat_confirm) {
        screen = Screen::battle_setup;
        const int side = player_side();
        cursor = side == 0 ? int(game.battle.attacker_index) : int(game.battle.defender_index);
    }
    else if(game.campaign.phase == sg::Phase::battle && screen != Screen::battle && screen != Screen::commands &&
            screen != Screen::battle_help &&
            screen != Screen::battle_setup && screen != Screen::battle_general_info &&
            screen != Screen::battle_retreat_confirm) { screen = Screen::battle; skill_slot = 0; }
    else if(game.campaign.phase == sg::Phase::prisoners && screen != Screen::prisoners &&
            screen != Screen::prisoner_release_confirm) { screen = Screen::prisoners; cursor = 0; }
    else if(game.campaign.phase == sg::Phase::ended && screen != Screen::ended &&
            screen != Screen::game_over && screen != Screen::victory_epilogue &&
            screen != Screen::victory_credits) {
        // demake persists GamePass in CheckGameState before displaying its
        // victory dialogue. Commit it on entry to the ended screen so a reset
        // during that dialogue cannot lose the unlock.
        if(player_unified() && !sg::game_pass_unlocked()) sg::unlock_game_pass();
        screen = Screen::ended;
    }
    else if(game.campaign.phase == sg::Phase::world && (screen == Screen::battle || screen == Screen::commands)) { screen = Screen::map; world_paused = true; }
}

bool start_cancels_menu(Screen page) {
    switch(page) {
    case Screen::title_confirm: case Screen::load_confirm:
    case Screen::prisoner_release_confirm: case Screen::save_slots:
    case Screen::save_overwrite: case Screen::domestic_generals:
    case Screen::city_prisoners: case Screen::city_prisoner_detail:
    case Screen::prisoner_persuasion: case Screen::troops:
    case Screen::troop_types: case Screen::promotion: case Screen::major:
    case Screen::expedition_abandon_confirm: case Screen::expedition_prefect:
    case Screen::pause: case Screen::domestic_finish_confirm: case Screen::help:
    case Screen::find_generals: case Screen::find_general_confirm:
    case Screen::power_map: case Screen::power_detail: case Screen::army_list:
    case Screen::army_commands: case Screen::army_target: case Screen::destination:
    case Screen::army_prisoners: case Screen::army_prisoner_detail:
    case Screen::army_detail: case Screen::army_general_info:
    case Screen::commands: case Screen::battle_retreat_confirm:
        return true;
    default: return false;
    }
}

void input() {
    if(screen!=Screen::battle) battle_shoulders.reset();
    static Screen repeat_screen=Screen::title;
    if(repeat_screen!=screen) {
        up_repeat=down_repeat=left_repeat=right_repeat=0;
        troop_l_repeat=troop_r_repeat=0;
        repeat_screen=screen;
    }
    bool up_pressed = bn::keypad::up_pressed(), up_held = bn::keypad::up_held();
    bool down_pressed = bn::keypad::down_pressed(), down_held = bn::keypad::down_held();
    bool left_pressed = bn::keypad::left_pressed(), left_held = bn::keypad::left_held();
    bool right_pressed = bn::keypad::right_pressed(), right_held = bn::keypad::right_held();
    bool up = repeat_direction(up_pressed,up_held,up_repeat);
    bool down = repeat_direction(down_pressed,down_held,down_repeat);
    bool left = repeat_direction(left_pressed,left_held,left_repeat);
    bool right = repeat_direction(right_pressed,right_held,right_repeat);
    bool a = bn::keypad::a_pressed(), b = bn::keypad::b_pressed();
    bool start = bn::keypad::start_pressed();
    bool select = bn::keypad::select_pressed();
    bool l = bn::keypad::l_pressed(), r = bn::keypad::r_pressed();
    bool l_held = bn::keypad::l_held(), r_held = bn::keypad::r_held();
    // Drop pending map singles outside the map, but keep held gestures consumed
    // until release. Returning with L+R held must not immediately reopen armies.
    if(screen!=Screen::map) map_shoulders.suspend(l_held,r_held);
    // Resolve cancel before any page can mutate a campaign or write Flash.
    // START remains confirm on pages where it starts a game/expedition/battle.
    // Do not apply this menu policy to real-time battle's B camera reset.
    const bool cancelling=screen!=Screen::battle &&
        (b || (start && start_cancels_menu(screen)) ||
         (select && screen==Screen::battle_retreat_confirm));
    if(cancelling) {
        a=up=down=left=right=l=r=false;
        l_held=r_held=false;
        if(b) start=select=false;
        else if(start) select=false;
    }
    if(screen!=Screen::battle) {
        if(b) sg::audio::play(sg::audio::Cue::back);
        else if(a || start) sg::audio::play(sg::audio::Cue::accept);
        else if(up || down || left || right || l || r || select) sg::audio::play(sg::audio::Cue::move);
    }
    // A map shoulder may still become a chord. Do not repaint the expensive
    // map for the first button before its actual action has been decided.
    redraw |= (bn::keypad::any_pressed() && (screen!=Screen::map || a || b || start || select)) ||
              up || down || left || right;
    // Local city editors belong to the player's cities, but faction-wide
    // domestic pages must remain available regardless of the map selection.
    const bool faction_roster_page=(screen==Screen::officers && officers_global_roster) ||
        (screen==Screen::items && items_global_roster) ||
        (screen==Screen::promotion && promotion_global_roster);
    if(!own_selected_city() && !faction_roster_page && (screen==Screen::officers || screen==Screen::items ||
       screen==Screen::troops || screen==Screen::troop_types || screen==Screen::promotion || screen==Screen::major ||
       screen==Screen::assemble || screen==Screen::commander)) {
        screen=Screen::city; cursor=officer_cursor=0; officer_detail=false;
        game.notice=""; redraw=true;
        return;
    }
    switch(screen) {
    case Screen::title_confirm:
        if(b || start) screen=Screen::pause;
        else if(a) return_to_title();
        break;
    case Screen::load_confirm:
        if(b || start) screen=Screen::save_slots;
        else if(a) {
            if(sg::load_game_slot(game,slot_cursor)) {
                game.notice="读取成功";
                screen=game.campaign.phase==sg::Phase::domestic ? Screen::pause : Screen::map;
                world_paused=true; map_zoomed=false; map_army_focus=-1; focus_player_city(); cursor=0;
            } else { game.notice="读取失败，当前进度保留"; screen=Screen::save_slots; }
        }
        break;
    case Screen::prisoner_release_confirm:
        if(b || start) { release_general=-1; screen=Screen::prisoners; }
        else if(a) {
            if(release_general>=0) game.handle_prisoner(release_general,false);
            release_general=-1; screen=Screen::prisoners;
            if(!prisoner_count()) { game.finish_prisoners(); screen=Screen::map; world_paused=true; }
        }
        break;
    case Screen::title:
        if(title_scenarios) {
            if(up) scenario = (scenario+4)%5;
            if(down) scenario = (scenario+1)%5;
            if(b) { title_scenarios = false; title_cursor = 0; }
            if(a || start) { screen = Screen::kings; king = 0; while(!sg::king_defs[scenario][king].selectable) ++king; }
        } else {
            if(up) title_cursor = title_cursor ? title_cursor - 1 : 1;
            if(down) title_cursor = title_cursor < 1 ? title_cursor + 1 : 0;
            if(a || start) {
                if(title_cursor == 0) {
                    title_message = nullptr;
                    if(sg::game_pass_unlocked()) title_scenarios=true;
                    else {
                        scenario=1; king=0;
                        while(!sg::king_defs[scenario][king].selectable) ++king;
                        screen=Screen::kings;
                    }
                }
                else if(title_cursor == 1) {
                    open_slot_page(false,false);
                }
            }
        }
        if(select && !title_scenarios) {
            open_slot_page(false,false);
        }
        break;
    case Screen::save_slots: {
        const int visible_slots=8;
        if(up) slot_cursor=(slot_cursor+visible_slots-1)%visible_slots;
        if(down) slot_cursor=(slot_cursor+1)%visible_slots;
        if(up || down) game.notice=""; // Do not carry another slot's error into its details.
        if(b || start) { screen=slot_page_from_pause ? Screen::pause : Screen::title; game.notice=""; }
        // Unknown is not empty. A during a pending check must never turn into
        // an implicit overwrite or a deferred load after validation finishes.
        if(a && !(slot_checked_mask&(1<<slot_cursor))) break;
        if(a) {
            if(slot_action_save) {
                if(slot_summaries[slot_cursor].valid)
                    screen=Screen::save_overwrite;
                else {
                    if(sg::save_game(game,slot_cursor)) {
                        game.notice="保存成功";
                        screen=slot_page_from_pause ? Screen::pause : Screen::map;
                        world_paused=true;
                    } else game.notice="保存失败";
                }
            } else if(slot_page_from_pause && slot_summaries[slot_cursor].valid) {
                screen=Screen::load_confirm;
            } else if(slot_summaries[slot_cursor].valid && sg::load_game_slot(game,slot_cursor)) {
                game.notice="读取成功";
                screen=game.campaign.phase==sg::Phase::domestic ? Screen::pause : Screen::map;
                world_paused=true; map_zoomed=false; map_army_focus=-1; focus_player_city(); cursor=0;
            } else game.notice="该槽没有有效记录";
        }
        break;
    }
    case Screen::save_overwrite:
        if(a) {
            if(sg::save_game(game,slot_cursor)) {
                game.notice="保存成功";
                screen=slot_page_from_pause ? Screen::pause : Screen::map;
                world_paused=true;
            } else { game.notice="保存失败"; screen=Screen::save_slots; }
        }
        if(b || start) screen=Screen::save_slots;
        break;
    case Screen::kings:
        if(up || down) {
            int direction = up ? -1 : 1, count = sg::scenario_defs[scenario].king_count;
            do { king = (king+direction+count)%count; } while(!sg::king_defs[scenario][king].selectable);
        }
        if(b) {
            screen=Screen::title;
            title_scenarios=sg::game_pass_unlocked();
            title_cursor=0;
        }
        if(a || start) screen = Screen::king_confirm;
        break;
    case Screen::king_confirm:
        if(a || start) {
            game.new_game(scenario,king); title_message = nullptr; screen = Screen::pause; cursor=0; world_paused = true; map_zoomed = false; map_army_focus=-1;
            focus_player_city();
        }
        if(b) screen = Screen::kings;
        break;
    case Screen::year_end_dialogue:
        if(a || start) {
            screen=Screen::pause;
            cursor=0;
            world_paused=true;
            game.notice="";
        }
        break;
    case Screen::map: {
        if(map_army_focus>=0 && !game.campaign.armies[map_army_focus].active) map_army_focus=-1;
        if(map_army_focus>=0) {
            int shoulder_action=0;
            if(a || b || start || select) map_shoulders.suspend(l_held,r_held);
            else shoulder_action=map_shoulders.update(l,r,l_held,r_held,false,1+bn::core::last_missed_frames());
            if(shoulder_action==2) { open_army_list();break; }
            if(shoulder_action) {
                for(int step=1;step<=sg::max_armies;++step) {
                    const int next=(map_army_focus+(shoulder_action>0 ? step : sg::max_armies-step))%sg::max_armies;
                    if(game.campaign.armies[next].active) { locate_map_army(next);break; }
                }
            }
            if(up) move_map_army(0,-1);
            if(down) move_map_army(0,1);
            if(left) move_map_army(-1,0);
            if(right) move_map_army(1,0);
            if(a) {
                const int selected=map_army_focus;
                open_army_list();
                for(int i=0;i<army_count;++i) if(army_indices[i]==selected) army_cursor=i;
                cursor=0;screen=Screen::army_commands;
            }
            if(b) { map_army_focus=-1;redraw=true; }
            if(select && game.campaign.phase==sg::Phase::world) world_paused=!world_paused;
            if(start) { screen=Screen::pause;cursor=0; }
            break;
        }
        if(b) map_zoomed = !map_zoomed;
        if(up && !move_city(0,-1)) game.notice = "该方向没有可选城池";
        if(down && !move_city(0,1)) game.notice = "该方向没有可选城池";
        if(left && !move_city(-1,0)) game.notice = "该方向没有可选城池";
        if(right && !move_city(1,0)) game.notice = "该方向没有可选城池";
        int shoulder_action=0;
        if(a || b || start) map_shoulders.suspend(bn::keypad::l_held(),bn::keypad::r_held());
        else shoulder_action=map_shoulders.update(l,r,l_held,r_held,false,1+bn::core::last_missed_frames());
        if(shoulder_action) {
            redraw=true;
            if(shoulder_action==2) { open_army_list(); break; }
            if(!own_city(shoulder_action)) game.notice = "当前没有我方城池";
        }
        if(select && game.campaign.phase == sg::Phase::world) world_paused = !world_paused;
        else if(select) game.notice = "内政阶段大地图时间暂停";
        if(a) { screen = Screen::city; cursor = 0; officer_cursor = 0; game.notice=""; }
        if(start) { screen = Screen::pause; cursor = 0; }
        break;
    }
    case Screen::city: {
        bool owned = game.campaign.cities[city].king == game.campaign.player;
        if(!owned) {
            if(b) { screen=Screen::map; world_paused|=game.campaign.phase!=sg::Phase::world; game.notice=""; }
            break;
        }
        move_city_menu(up,down,left,right);
        int count = officer_count();
        if(l && count) officer_cursor = (officer_cursor+count-1)%count;
        if(r && count) officer_cursor = (officer_cursor+1)%count;
        if(up || down || l || r) { game.notice=""; game.search_result_general=-1; }
        if(b) { screen=Screen::map; world_paused|=game.campaign.phase!=sg::Phase::world; }
        if(a) {
            if(game.campaign.phase == sg::Phase::domestic) {
                if(cursor == 3) { screen = Screen::officers; officer_cursor = 0; officer_detail = false; game.notice = ""; }
                else if(cursor == 5) game.notice = "请先结束内政，再出征";
                else if(cursor == 4) {
                    if(owned && count)
                        { screen = Screen::promotion; cursor = 0; game.notice = ""; }
                    else game.notice = "本城没有可晋升的我方武将";
                }
                else if(owned && cursor <= 2) {
                    domestic_action=domestic_operation_for_command(cursor);
                    if(domestic_command_available(cursor)) {
                        collect_domestic_generals(domestic_action);
                        domestic_roster_cursor=0;
                        domestic_from_pause=false;
                        screen=Screen::domestic_generals;
                        game.notice="";
                    } else game.notice="没有符合能力的驻城武将";
                }
            } else {
                if(cursor == 0) {
                    if(owned && count) {
                        screen = Screen::assemble; cursor = 0; officer_cursor = 0;
                        group_count = 0; expedition_funds = 0; game.notice = "";
                    }
                    else game.notice = "只有我方城池可以出征";
                }
                else if(cursor == 1) { screen = Screen::troops; officer_cursor = 0; game.notice = ""; }
                else if(cursor == 2) { screen = Screen::officers; officer_cursor = 0; officer_detail = false; game.notice = ""; }
                else if(cursor == 3) {
                    if(city_prisoner_count(city)) open_city_prisoners();
                    else game.notice="本城没有俘虏";
                }
                else if(cursor == 4) {
                    const int ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
                    if(ruler>=0 && game.campaign.officers[ruler].city!=city) {
                        screen = Screen::major; officer_cursor = 0; game.notice = "";
                    }
                }
            }
        }
        break;
    }
    case Screen::domestic_generals: {
        const int count=collect_domestic_generals(domestic_action);
        if(count) {
            domestic_roster_cursor%=count;
            if(up) domestic_roster_cursor=(domestic_roster_cursor+count-1)%count;
            if(down) domestic_roster_cursor=(domestic_roster_cursor+1)%count;
            if(up || down) game.notice="";
            if(a) {
                const int general=domestic_generals[domestic_roster_cursor];
                const int general_city=game.campaign.officers[general].city;
                if(!game.campaign.officers[general].worked) {
                    const auto population_before=game.campaign.cities[general_city].population;
                    const int defense_before=game.campaign.cities[general_city].defense;
                    const int inventory_before=sg::city_inventory_count(game.campaign,general_city);
                    if(game.domestic_action(general_city,general,domestic_action)) {
                        city=general_city;
                        domestic_result_worker=general;
                        domestic_result_city=general_city;
                        domestic_result_item=-1;
                        domestic_result_delta=domestic_action==0 ?
                            game.campaign.cities[general_city].population-population_before :
                            domestic_action==1 ? game.campaign.cities[general_city].defense-defense_before : 0;
                        if(domestic_action==2 && sg::city_inventory_count(game.campaign,general_city)>inventory_before)
                            domestic_result_item=sg::city_inventory_item(game.campaign,general_city,
                                sg::city_inventory_count(game.campaign,general_city)-1);
                        screen=Screen::domestic_result;
                    }
                }
            }
        }
        if(b || start) {
            if(domestic_from_pause) cursor=5-domestic_action;
            screen=domestic_from_pause ? Screen::pause : Screen::city;
            domestic_from_pause=false;
            game.notice="";
        }
        break;
    }
    case Screen::domestic_result:
        if(a || b || start) screen=Screen::domestic_generals;
        break;
    case Screen::city_prisoners: {
        const int count=city_prisoner_count(city);
        if(count) {
            city_prisoner_cursor%=count;
            if(up) city_prisoner_cursor=(city_prisoner_cursor+count-1)%count;
            if(down) city_prisoner_cursor=(city_prisoner_cursor+1)%count;
            if(a) screen=Screen::city_prisoner_detail;
        }
        if(b || start) { screen=Screen::city; cursor=3; }
        break;
    }
    case Screen::city_prisoner_detail: {
        const int count=city_prisoner_count(city);
        if(count) {
            city_prisoner_cursor%=count;
            if(left) city_prisoner_cursor=(city_prisoner_cursor+count-1)%count;
            if(right) city_prisoner_cursor=(city_prisoner_cursor+1)%count;
        }
        if(b || start) screen=Screen::city_prisoners;
        break;
    }
    case Screen::prisoner_persuasion: {
        const int count=prisoner_count();
        if(count) {
            prisoner_cursor%=count;
            if(up) prisoner_cursor=(prisoner_cursor+count-1)%count;
            if(down) prisoner_cursor=(prisoner_cursor+1)%count;
            if(up || down) game.notice="";
            if(a) {
                const int general=prisoners[prisoner_cursor];
                game.persuade_prisoner(general);
            }
        }
        if(b || start) { screen=Screen::pause; cursor=2; game.notice=""; }
        break;
    }
    case Screen::officers: { // input
        int count = officer_roster_count();
        if(count) {
            const int previous_officer=officer_cursor;
            if(up) officer_cursor = (officer_cursor+count-1)%count;
            if(down) officer_cursor = (officer_cursor+1)%count;
            if(officer_cursor!=previous_officer) game.notice="";
            auto& o = game.campaign.officers[officers[officer_cursor]];
            if(!officer_detail) {
                if(l) officer_cursor = sg::ui::page_step(officer_cursor,count,-1);
                if(r) officer_cursor = sg::ui::page_step(officer_cursor,count,1);
                if(officer_cursor!=previous_officer) game.notice="";
            } else if(!officers_global_roster) {
                if(l && o.king == game.campaign.player) {
                    int next = o.force;
                    for(int tries = 0; tries < sg::unit_types; ++tries) {
                        next = (next+1)%sg::unit_types;
                        if((o.forces >> next)&1) {
                            game.notice=next==o.force ? "仅掌握当前兵种" : "兵种已切换";
                            o.force=uint8_t(next);break;
                        }
                    }
                }
                if(r && o.king == game.campaign.player) {
                    int next = o.formation;
                    for(int tries = 0; tries < 7; ++tries) {
                        next = (next+1)%7;
                        if((o.formations >> next)&1) {
                            game.notice=next==o.formation ? "仅掌握当前阵形" : "阵形已切换";
                            o.formation=uint8_t(next);break;
                        }
                    }
                }
            } else {
                if(l) officer_cursor=(officer_cursor+count-1)%count;
                if(r) officer_cursor=(officer_cursor+1)%count;
                if(officer_cursor!=previous_officer) game.notice="";
            }
        }
        if(select) {
            const int ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
            if(game.campaign.phase == sg::Phase::world &&
               game.campaign.cities[city].king == game.campaign.player &&
               (ruler<0 || game.campaign.officers[ruler].city!=city)) {
                screen = Screen::major; officer_cursor = 0; game.notice = "";
            }
        }
        else if(start) officer_detail = !officer_detail;
        else if(a && officer_detail && !officers_global_roster) { items_global_roster=false; screen = Screen::items; item_cursor = 0; game.notice = ""; }
        else if(a && !officer_detail) officer_detail=true;
        else if(b && officer_detail) { officer_detail=false; game.notice=""; }
        else if(b && officers_global_roster) { officers_global_roster=false; screen=Screen::pause; cursor=1; game.notice=""; }
        else if(b) { screen = Screen::city; officer_detail = false; game.notice = ""; }
        break;
    }
    case Screen::items: {
        // SELECT is an explicit unequip action, not an additional action to A.
        // Never consume a token or equip another object before handling it.
        if(select) a=false;
        int count = item_officer_count();
        if(count) {
            officer_cursor %= count;
            if(l || r) {
                const int next=(officer_cursor + (r ? 1 : count-1)) % count;
                if(next!=officer_cursor) { item_cursor=0;game.notice=""; }
                officer_cursor=next;
            }
            auto& o = game.campaign.officers[officers[officer_cursor]];
            const int city_rows=owned_city_item_count(game.campaign,o.king);
            const int legacy_rows=0;
            const int equipped_row=city_rows+legacy_rows;
            const int rows=equipped_row+(o.equipped_item ? 1 : 0);
            item_cursor=rows ? (item_cursor<rows ? item_cursor : rows-1) : 0;
            if(rows>0 && up) item_cursor=(item_cursor+rows-1)%rows;
            if(rows>0 && down) item_cursor=(item_cursor+1)%rows;
            if(a && item_cursor<city_rows) {
                int object_city=-1,object_index=-1;
                if(owned_city_item_location(game.campaign,o.king,item_cursor,object_city,object_index)) {
                    const uint8_t entry=sg::city_inventory_item(game.campaign,object_city,object_index);
                    if(!general_has_object(officers[officer_cursor],sg::city_inventory_type(entry),sg::city_inventory_index(entry)))
                        game.use_city_object(officers[officer_cursor],object_city,object_index);
                }
            } else if(a && item_cursor<equipped_row) {
                const int item=sg::faction_inventory_item(game.campaign,o.king,item_cursor-city_rows);
                if(!faction_item_owned_by_general(officers[officer_cursor],item))
                    game.use_item(officers[officer_cursor],item_cursor-city_rows);
            } else if(a && o.equipped_item) {
                const int home=item_officer_city(officers[officer_cursor]);
                if(home>=0) game.use_city_object(officers[officer_cursor],home,-1);
            }
            else if(a) game.notice="\u6ca1\u6709\u53ef\u4f7f\u7528\u6216\u88c5\u5907\u7684\u7269\u54c1";
            if(select && o.equipped_item) {
                const int home=item_officer_city(officers[officer_cursor]);
                if(home>=0) game.use_city_object(officers[officer_cursor],home,-1);
            }
            else if(select) game.notice="\u5f53\u524d\u6ca1\u6709\u88c5\u5907\u53ef\u5378\u4e0b";
            if(a || select) {
                const int remaining=owned_city_item_count(game.campaign,o.king)+(o.equipped_item ? 1 : 0);
                item_cursor=remaining ? (item_cursor<remaining ? item_cursor : remaining-1) : 0;
            }
            if(b) { screen=items_global_roster ? Screen::pause : Screen::officers; items_global_roster=false; game.notice=""; }
        } else if(b) { screen=items_global_roster ? Screen::pause : Screen::officers; items_global_roster=false; game.notice=""; }
        break;
    }
    case Screen::troops: {
        int count = officer_count();
        const bool add_one=repeat_direction(l,l_held,troop_l_repeat);
        const bool remove_one=repeat_direction(r,r_held,troop_r_repeat);
        if(count) {
            officer_cursor %= count;
            if(up) officer_cursor = (officer_cursor+count-1)%count;
            if(down) officer_cursor = (officer_cursor+1)%count;
            if(up || down) game.notice="";
            const int g = officers[officer_cursor];
            if(select) {
                troop_type_count=collect_troop_types(g);
                troop_type_cursor=0;
                for(int i=0;i<troop_type_count;++i)
                    if(troop_type_list[i]==game.campaign.officers[g].force) troop_type_cursor=i;
                screen=Screen::troop_types;
            } else if(add_one) {
                // Ten at a time, like the funds page: reserve caps run to a few
                // hundred, so single-soldier transfer meant holding R for a
                // minute to unload one general.
                game.notice = game.transfer_soldiers(city,g,10) ? "已补充兵力" :
                    game.campaign.cities[city].reserve ? "该武将兵力已满" : "城中没有预备兵";
            } else if(remove_one) {
                game.notice = game.transfer_soldiers(city,g,-10) ? "已卸下兵力" :
                    game.campaign.cities[city].reserve >= sg::original::demake_reserve_capacity(city) ? "预备兵已满" : "该武将没有可卸下的兵";
            } else if(a) {
                if(game.transfer_soldiers(city,g,game.max_soldiers(g)))
                    game.notice=game.campaign.officers[g].soldiers==game.max_soldiers(g) ?
                        "已补至最大兵力" : "预备兵不足，未补满";
                else game.notice=game.campaign.cities[city].reserve ? "该武将兵力已满" : "城中没有预备兵";
            }
        }
        else if(l || r) game.notice = "本城没有可调兵的武将";
        if(b || start) { screen = Screen::city; game.notice = ""; troop_l_repeat=troop_r_repeat=0; }
        break;
    }
    case Screen::troop_types: {
        const int count=officer_count();
        if(!count) { screen=Screen::troops; troop_type_count=0; break; }
        officer_cursor%=count;
        const int general=officers[officer_cursor];
        troop_type_count=collect_troop_types(general);
        if(troop_type_count) {
            troop_type_cursor%=troop_type_count;
            if(up) troop_type_cursor=(troop_type_cursor+troop_type_count-1)%troop_type_count;
            if(down) troop_type_cursor=(troop_type_cursor+1)%troop_type_count;
            if(a) {
                game.campaign.officers[general].force=uint8_t(troop_type_list[troop_type_cursor]);
                game.notice="兵种已切换";
                screen=Screen::troops;
            }
        }
        if(b || start) screen=Screen::troops;
        break;
    }
    case Screen::promotion: {
        int count = promotion_officer_count();
        if(count) {
            officer_cursor %= count;
            if(l || r) {
                const int next=(officer_cursor+(r?1:count-1))%count;
                if(next!=officer_cursor) { cursor=0; game.notice=""; }
                officer_cursor=next;
            }
            const int g=officers[officer_cursor], tier=promotion_tier(g);
            if(tier == 3) cursor = 0;
            else if(tier) {
                if(up) cursor=(cursor+2)%3;
                if(down) cursor=(cursor+1)%3;
            }
            if(a) {
                const bool appointed=tier && game.appoint_skill(g,tier,cursor);
                game.notice = game.level(g) < 10 ? "需要达到十级才能册封技能" : !tier ?
                    "该武将没有待晋升技能" : appointed ? "册封成功" : "该武将当前无法册封";
                if(appointed) {
                    // The eligible roster can drop this general immediately.
                    // Name the recipient so the next displayed general cannot
                    // be mistaken for the one who just received the skill.
                    int used=0;
                    const char* part=sg::general_defs[g].name;
                    while(*part && used<47) promotion_success_notice[used++]=*part++;
                    part="册封成功";
                    while(*part && used<47) promotion_success_notice[used++]=*part++;
                    promotion_success_notice[used]=0;
                    game.notice=promotion_success_notice;
                    cursor=0;
                }
            }
        }
        if(b || start) {
            if(promotion_global_roster) { promotion_global_roster=false; screen=Screen::pause; cursor=7; }
            else { screen=Screen::city; cursor=4; }
        }
        break;
    }
    case Screen::major: {
        int count=officer_count();
        if(count) { officer_cursor%=count; if(up) officer_cursor=(officer_cursor+count-1)%count; if(down) officer_cursor=(officer_cursor+1)%count;
            if(a) {
                if(game.appoint_major(city,officers[officer_cursor])) { game.notice = ""; screen=Screen::major_result; }
                else game.notice = "该武将不在本城，无法任命";
            }
        }
        if(b || start) screen=Screen::city;
        break;
    }
    case Screen::major_result:
        if(a || start) { screen=Screen::city; game.notice=""; }
        break;
    case Screen::destination: {
        if(b) map_zoomed=!map_zoomed;
        if(start) { screen=Screen::map; world_paused=false; game.notice=""; break; }
        int count=0;
        const uint64_t reachable=game.reachable_city_mask(city);
        int nearby[sg::city_count];
        const int nearby_count=game.neighbors(city,nearby);
        for(int i=0;i<nearby_count;++i)
            if((reachable>>nearby[i])&1) destinations[count++]=nearby[i];
        for(int target=0;target<sg::city_count;++target) if(target!=city && ((reachable>>target)&1)) {
            bool listed=false;
            for(int i=0;i<count;++i) listed |= destinations[i]==target;
            if(!listed) destinations[count++]=target;
        }
        // demake also accepts the departure city; keep it after outward routes
        // so the default still sends the army toward the first nearby city.
        if((reachable>>city)&1) destinations[count++]=city;
        if(count <= 0) {
            break;
        }
        if(cursor >= count) cursor=0;
        if(up) move_target_city(cursor,count,0,-1);
        if(down) move_target_city(cursor,count,0,1);
        if(left) move_target_city(cursor,count,-1,0);
        if(right) move_target_city(cursor,count,1,0);
        if(a) {
            destination=destinations[cursor];
            const int army_index=game.campaign.officers[group[0]].army;
            const bool redirected=game.redirect_army(army_index,destination);
            if(redirected) {
                screen=Screen::map; world_paused=false; game.notice="";
            }
        }
        break;
    }
    case Screen::assemble: {
        int count = officer_count();
        if(up && count) officer_cursor = (officer_cursor+count-1)%count;
        if(down && count) officer_cursor = (officer_cursor+1)%count;
        if(a && count) {
            int g = officers[officer_cursor], existing = -1;
            for(int i = 0; i < group_count; ++i) if(group[i] == g) existing = i;
            if(existing >= 0) { for(int i = existing+1; i < group_count; ++i) group[i-1] = group[i]; --group_count; game.notice = ""; }
            else if(game.campaign.officers[g].hp==0) game.notice="体力为零，暂不能出征";
            else if(group_count < 5) { group[group_count++] = g; game.notice = ""; }
            else game.notice = "一支部队最多编入五名武将";
        }
        if(b) screen = Screen::city;
        if(start && group_count) { screen = Screen::expedition_funds; cursor = 0; }
        else if(start) game.notice = "至少选择一名武将再出征";
        break;
    }
    case Screen::expedition_funds: {
        const int available=int(game.campaign.cities[city].money);
        auto change_funds=[&](int delta) { expedition_funds=sg::clamp(expedition_funds+delta,0,available); };
        if(up || right) change_funds(100);
        if(down || left) change_funds(-100);
        if(r) change_funds(1000);
        if(l) change_funds(-1000);
        if(a && group_count) {
            game.notice="";
            int stationed[sg::general_count];
            const int stationed_count=game.officers_at(city,game.campaign.player,stationed,sg::general_count);
            if(stationed_count==group_count) {
                expedition_abandon_return=Screen::assemble;
                screen=Screen::expedition_abandon_confirm;
                cursor=0;
            } else {
                int ordered[5];
                for(int i=0;i<group_count;++i) ordered[i]=group[i];
                const int prefect=game.campaign.cities[city].major;
                int prefect_position=-1;
                for(int i=0;i<group_count;++i) if(ordered[i]==prefect) prefect_position=i;
                if(prefect_position>=0) {
                    ordered[prefect_position]=ordered[0];
                    ordered[0]=prefect;
                    for(int i=0;i<group_count;++i) group[i]=ordered[i];
                    expedition_prefect_count=0;
                    for(int i=0;i<stationed_count;++i) {
                        bool departs=false;
                        for(int j=0;j<group_count;++j) departs |= stationed[i]==ordered[j];
                        if(!departs) expedition_prefect_candidates[expedition_prefect_count++]=stationed[i];
                    }
                    if(expedition_prefect_count>1) {
                        screen=Screen::expedition_prefect;
                        cursor=0;
                    } else create_pending_expedition(ordered,group_count,false);
                } else if(group_count==1) {
                    create_pending_expedition(ordered,group_count,false);
                } else {
                    screen=Screen::commander; cursor=0;
                }
            }
        }
        if(b) screen=Screen::assemble;
        break;
    }
    case Screen::commander:
        if(group_count) {
            cursor %= group_count;
            if(up) cursor = (cursor + group_count - 1) % group_count;
            if(down) cursor = (cursor + 1) % group_count;
            if(a) {
                int selected = group[cursor];
                int ordered[5];
                for(int i = 0; i < group_count; ++i) ordered[i] = group[i];
                ordered[cursor] = ordered[0];
                ordered[0] = selected;
                create_pending_expedition(ordered,group_count,false);
            }
        }
        if(b) screen = Screen::assemble;
        break;
    case Screen::expedition_abandon_confirm:
        if(a && group_count) create_pending_expedition(group,group_count,true);
        else if(b || start) screen=expedition_abandon_return;
        break;
    case Screen::expedition_prefect:
        if(expedition_prefect_count) {
            cursor%=expedition_prefect_count;
            if(up) cursor=(cursor+expedition_prefect_count-1)%expedition_prefect_count;
            if(down) cursor=(cursor+1)%expedition_prefect_count;
            if(a) {
                const int replacement=expedition_prefect_candidates[cursor];
                if(create_pending_expedition(group,group_count,false) &&
                   !game.appoint_major(city,replacement)) screen=Screen::assemble;
            }
        }
        if(b || start) screen=Screen::assemble;
        break;
    case Screen::pause:
        if(up || down || left || right) game.notice="";
        if(up || down) {
            const int columns=pause_columns();
            const int rows=(pause_action_count()+columns-1)/columns, column=cursor%columns;
            int row=cursor/columns+(down ? 1 : -1);
            row=(row+rows)%rows;
            cursor=row*columns+column;
            if(cursor>=pause_action_count()) cursor=row*columns;
        }
        if(left && cursor%pause_columns()) --cursor;
        if(right && cursor%pause_columns()+1<pause_columns() && cursor+1<pause_action_count()) ++cursor;
        if(b || start) screen = Screen::map;
        if(a) {
            if(game.campaign.phase == sg::Phase::domestic) {
                if(cursor == 0) open_domestic_items();
                if(cursor == 1) open_global_officers();
                if(cursor == 2) open_prisoner_persuasion();
                if(cursor >= 3 && cursor <= 5) {
                    const int action=5-cursor;
                    if(domestic_action_available(action)) {
                        domestic_action=action;
                        domestic_roster_cursor=0;
                        domestic_from_pause=true;
                        collect_domestic_generals(action);
                        game.notice="";
                        screen=Screen::domestic_generals;
                    } else game.notice="没有符合能力的武将";
                }
                if(cursor == 6) open_power_map();
                if(cursor == 7) {
                    promotion_global_roster=true; officer_cursor=0; cursor=0; game.notice="";
                    screen=Screen::promotion;
                }
                if(cursor == 8) { game.notice=""; screen=Screen::domestic_finish_confirm; }
                if(cursor == 9) open_slot_page(true,true);
                if(cursor == 10) screen=Screen::title_confirm;
                if(cursor == 11) screen = Screen::map;
                if(cursor == 12) open_slot_page(false,true);
                if(cursor == 13) open_find_generals();
                if(cursor == 14) screen = Screen::help;
            } else {
                if(cursor == 0) { screen = Screen::map; world_paused = false; }
                if(cursor == 1) open_slot_page(true,true);
                if(cursor == 2) {
                    open_slot_page(false,true);
                }
                if(cursor == 3) open_find_generals();
                if(cursor == 4) open_power_map();
                if(cursor == 5) screen = Screen::help;
                if(cursor == 6) screen=Screen::title_confirm;
                if(cursor == 7) {
                    map_speed = map_speed >= 4 ? 1 : map_speed >= 2 ? 4 : 2;
                    game.notice = "";
                }
            }
        }
        break;
    case Screen::domestic_finish_confirm:
        if(a) {
            const bool first_domestic=game.campaign.year==sg::scenario_defs[game.campaign.scenario].year &&
                game.campaign.month_ticks==0;
            game.finish_domestic(!first_domestic);
            if(first_domestic) { screen=Screen::city; cursor=0; world_paused=true; }
            else { screen=Screen::map; world_paused=false; }
        }
        else if(b || start) { screen=Screen::pause; cursor=8; game.notice=""; }
        break;
    case Screen::help:
        if(select) { audio_options=!audio_options; audio_cursor=0; }
        if(audio_options) {
            if(up || down) audio_cursor=(audio_cursor+(up ? 2 : 1))%3;
            if(left || right || a) {
                if(audio_cursor==0) sg::audio::set_volume(sg::clamp(sg::audio::volume()+(left ? -1 : 1),0,4));
                else if(audio_cursor==1) sg::audio::set_music_enabled(!sg::audio::music_enabled());
                else sg::audio::set_effects_enabled(!sg::audio::effects_enabled());
            }
        }
        if(b || start) { screen = Screen::pause; audio_options=false; }
        break;
    case Screen::find_generals:
        if(find_general_count) {
            if(up) officer_cursor=(officer_cursor+find_general_count-1)%find_general_count;
            if(down) officer_cursor=(officer_cursor+1)%find_general_count;
            if(l) officer_cursor=sg::ui::page_step(officer_cursor,find_general_count,-1);
            if(r) officer_cursor=sg::ui::page_step(officer_cursor,find_general_count,1);
            if(a) { find_selected_general=officers[officer_cursor]; screen=Screen::find_general_confirm; }
        }
        if(b || start) screen=Screen::pause;
        break;
    case Screen::find_general_confirm:
        if(a && find_selected_general>=0) {
            const auto& officer=game.campaign.officers[find_selected_general];
            world_paused=true; map_zoomed=false; map_army_focus=-1; game.notice="";
            if(officer.city>=0 && officer.city<sg::city_count) {
                city=officer.city; cursor=0; officer_cursor=0;
                const int count=officer_count();
                for(int i=0;i<count;++i) if(officers[i]==find_selected_general) { officer_cursor=i; break; }
                screen=Screen::city;
            } else {
                int army_position=-1; army_count=0;
                for(int index=0;index<sg::max_armies;++index) if(game.campaign.armies[index].active) {
                    army_indices[army_count]=index;
                    for(int g=0;g<game.campaign.armies[index].count;++g)
                        if(game.campaign.armies[index].generals[g]==find_selected_general) army_position=army_count;
                    ++army_count;
                }
                if(army_position>=0) { army_cursor=army_position; cursor=0; screen=Screen::army_commands; }
                else screen=Screen::find_generals;
            }
        }
        if(b || start) screen=Screen::find_generals;
        break;
    case Screen::power_map:
        if(power_force_count) {
            if(up) power_cursor=(power_cursor+power_force_count-1)%power_force_count;
            if(down) power_cursor=(power_cursor+1)%power_force_count;
            if(a) { power_city_cursor=0; screen=Screen::power_detail; }
        }
        if(b || start) screen=Screen::pause;
        break;
    case Screen::power_detail: {
        const int force=power_force_count ? power_forces[power_cursor] : -1;
        const int count=force>=0 ? force_city_count(force) : 0;
        if(count && up) power_city_cursor=(power_city_cursor+count-1)%count;
        if(count && down) power_city_cursor=(power_city_cursor+1)%count;
        if(a && count) {
            const int target=force_city_at(force,power_city_cursor);
            if(target>=0) { city=target; map_army_focus=-1; world_paused=true; map_zoomed=false; screen=Screen::map; game.notice="已定位到所选势力城池"; }
        }
        if(b || start) screen=Screen::power_map;
        break;
    }
    case Screen::army_list:
        if(army_count) {
            if(up) army_cursor=(army_cursor+army_count-1)%army_count;
            if(down) army_cursor=(army_cursor+1)%army_count;
            if(a) { cursor=0; screen=Screen::army_commands; }
            if(select) locate_map_army(army_indices[army_cursor]);
        }
        if(b || start) screen=Screen::map;
        break;
    case Screen::army_commands: {
        if(army_count) {
            const int army_index=army_indices[army_cursor];
            const auto& army=game.campaign.armies[army_index];
            if(select) { locate_map_army(army_index); break; }
            if(up) cursor=(cursor+5)%6;
            if(down) cursor=(cursor+1)%6;
            if(up || down) game.notice="";
            if(a && army.king!=game.campaign.player) game.notice="敌方不可指挥";
            else if(a) {
                if(cursor==0 && army.king==game.campaign.player) {
                    army_general_cursor=0; screen=Screen::army_detail;
                }
                else if(cursor==1 && army.king==game.campaign.player) {
                    int current=army.garrisoned || army.remaining*2<=army.total ? army.target : army.origin;
                    const uint64_t reachable=game.reachable_city_mask(current);
                    army_target_count=0;
                    for(int target=0;target<sg::city_count;++target)
                        // demake accepts every reachable city, including the
                        // current destination and the previous route origin.
                        if((reachable>>target)&1)
                            destinations[army_target_count++]=target;
                    army_target_cursor=0;
                    screen=Screen::army_target;
                    redraw=true;
                }
                else if(cursor==2 && game.garrison_army(army_index)) {
                    screen=Screen::map; game.notice="";
                }
                else if(cursor==3 && army.king==game.campaign.player && count_army_prisoners(army_index))
                    open_army_prisoners(army_index);
                else if(cursor==4 && game.enter_army_city(army_index)) {
                    screen=Screen::map; game.notice="";
                }
                else if(cursor==5 && game.resume_army(army_index)) {
                    screen=Screen::map;game.notice="";
                }
                else if(cursor==3) game.notice="没有随军俘虏";
                else if(cursor==4) game.notice="需靠近我方城";
                else if(cursor==5) game.notice=army.garrisoned ? "请先指定目标" : "部队正在行军";
            }
        }
        if(b || start) { screen=Screen::map;game.notice=""; }
        break;
    }
    case Screen::army_target: {
        if(b) map_zoomed=!map_zoomed;
        if(army_target_count) {
            if(up) move_target_city(army_target_cursor,army_target_count,0,-1);
            if(down) move_target_city(army_target_cursor,army_target_count,0,1);
            if(left) move_target_city(army_target_cursor,army_target_count,-1,0);
            if(right) move_target_city(army_target_cursor,army_target_count,1,0);
            if(a && army_count) {
                const int target=destinations[army_target_cursor];
                const bool redirected=game.redirect_army(army_indices[army_cursor],target);
                if(redirected) {
                    city=target;
                    screen=Screen::map; game.notice=""; redraw=true;
                    if(map_army_focus>=0) locate_map_army(army_indices[army_cursor]);
                } else game.notice="无法改道，请先处理城池遭遇";
            }
        }
        // demake SelectTargetCity.GetBack() closes the selector and returns
        // directly to Strategy's map mode, not to ArmyCommandsController.
        if(start) screen=Screen::map;
        break;
    }
    case Screen::army_prisoners:
        if(army_prisoner_count && up) army_prisoner_cursor=(army_prisoner_cursor+army_prisoner_count-1)%army_prisoner_count;
        if(army_prisoner_count && down) army_prisoner_cursor=(army_prisoner_cursor+1)%army_prisoner_count;
        if(army_prisoner_count && a) { army_prisoner_info_section=0; screen=Screen::army_prisoner_detail; }
        if(b || start) screen=Screen::army_commands;
        break;
    case Screen::army_prisoner_detail:
        if(army_prisoner_count) {
            army_prisoner_cursor%=army_prisoner_count;
            if(left && army_prisoner_cursor>0) --army_prisoner_cursor;
            if(right && army_prisoner_cursor+1<army_prisoner_count) ++army_prisoner_cursor;
            if(l) army_prisoner_info_section=(army_prisoner_info_section+4)%5;
            if(r) army_prisoner_info_section=(army_prisoner_info_section+1)%5;
        }
        if(b || start) screen=Screen::army_prisoners;
        break;
    case Screen::army_detail: {
        const sg::Army& army=game.campaign.armies[army_indices[army_cursor]];
        if(army.count) {
            army_general_cursor%=army.count;
            if(up) army_general_cursor=(army_general_cursor+army.count-1)%army.count;
            if(down) army_general_cursor=(army_general_cursor+1)%army.count;
            if(a) { army_prisoner_info_section=0; screen=Screen::army_general_info; }
        }
        if(b || start) screen=Screen::army_commands;
        break;
    }
    case Screen::army_general_info: {
        const sg::Army& army=game.campaign.armies[army_indices[army_cursor]];
        if(army.count) {
            army_general_cursor%=army.count;
            if(left && army_general_cursor>0) --army_general_cursor;
            if(right && army_general_cursor+1<army.count) ++army_general_cursor;
            if(l) army_prisoner_info_section=(army_prisoner_info_section+4)%5;
            if(r) army_prisoner_info_section=(army_prisoner_info_section+1)%5;
        }
        if(b || start) screen=Screen::army_detail;
        break;
    }
    case Screen::battle: {
        const bool zoom_chord=bn::keypad::l_held() && r_held;
        const bool row_pan = !zoom_chord && r_held && (up_held || down_held);
        const int shoulder_action=battle_shoulders.update(l,r,l_held,r_held,row_pan,1+bn::core::last_missed_frames());
        if(shoulder_action==2) { battle_fit_all=!battle_fit_all; redraw=true; }
        if(row_pan) {
            battle_fit_all=false;
            game.battle.camera_y_manual = 1;
            const int pan_step = 3;
            const int delta = ((down_held ? pan_step : 0) - (up_held ? pan_step : 0)) * (1+bn::core::last_missed_frames());
            game.battle.camera_y = int16_t(sg::clamp(int(game.battle.camera_y)+delta,0,sg::battle_grid_max_y));
            redraw = true;
        } else if(up || down) {
            battle_fit_all=false;
            game.battle.camera_manual = 1;
            game.battle.camera_y_manual = 0;
            const int focus_side = up ? player_side() : 1-player_side();
            game.battle.camera_x = int16_t(sg::clamp(int(game.battle.sides[focus_side].x)-sg::battle_view_width/2,0,sg::battle_camera_max_x(game)));
            redraw = true;
        }
        if(bn::keypad::left_held() || bn::keypad::right_held()) {
            battle_fit_all=false;
            game.battle.camera_manual = 1;
            const int pan_step = 6;
            const int delta = sg::battle_camera_pan_direction(game,
                (bn::keypad::right_held() ? pan_step : 0) - (bn::keypad::left_held() ? pan_step : 0)) * (1+bn::core::last_missed_frames());
            game.battle.camera_x = int16_t(sg::clamp(int(game.battle.camera_x)+delta,0,sg::battle_camera_max_x(game)));
            redraw = true;
        }
        if(b) {
            battle_fit_all=false;
            game.battle.camera_manual = 0;
            game.battle.camera_y_manual = 0;
            const int focus_side = player_side();
            game.battle.camera_x = int16_t(sg::clamp(
                int(game.battle.sides[focus_side].x)-sg::battle_view_width/2,
                0,sg::battle_camera_max_x(game)));
            game.battle.camera_y = sg::battle_commander_y;
            redraw = true;
        }
        int side = player_side();
        bool controlled = game.campaign.officers[game.battle.sides[side].general].king == game.campaign.player;
        int unlocked = game.skill_count(game.battle.sides[side].general);
        skill_slot %= unlocked;
        if(shoulder_action==-1) { skill_slot = (skill_slot+unlocked-1)%unlocked; redraw=true; }
        if(shoulder_action==1) { skill_slot = (skill_slot+1)%unlocked; redraw=true; }
        if(a && controlled) {
            if(game.cast_skill(side,skill_slot)) {
                sg::audio::play(sg::audio::Cue::magic);
                battle_notice = nullptr; battle_notice_ticks = 0;
            } else {
                // cast_skill can reject without setting game.notice; do not
                // display a stale campaign message in the battle footer.
                const auto* selected_skill=sg::skill_by_id(game.learned_skill(game.battle.sides[side].general,skill_slot));
                battle_notice = game.battle.sides[side].order==sg::Order::retreat ? "撤退中无法施技" :
                    !selected_skill ? "没有可用武将技" :
                    game.battle.sides[side].mp < selected_skill->mp ? "技力不足" :
                    game.battle.sides[side].charge < sg::original::cast_charge_max ? "集气未满" :
                    game.battle.effect_remaining ? "请等待当前技能结束" : "当前无法施放";
                battle_notice_ticks = 60; // One second of hardware time.
                redraw = true;
            }
        }
        if(start && controlled) {
            screen = Screen::commands;
            game.notice="";
            const auto order=game.battle.sides[side].order;
            cursor=order==sg::Order::advance ? 5 : order==sg::Order::front ? 0 :
                order==sg::Order::front_back ? 1 : order==sg::Order::general ? 6 :
                order==sg::Order::retreat ? 7 : 4;
            game.battle.paused = 1;
        }
        if(select) { battle_speed_up=!battle_speed_up; redraw=true; }
        break;
    }
    case Screen::battle_setup: {
        const int side = player_side();
        const int count = side == 0 ? int(game.battle.attacker_count) : int(game.battle.defender_count);
        if(battle_setup_actions) {
            if(up) battle_setup_action_cursor = (battle_setup_action_cursor + 2) % 3;
            if(down) battle_setup_action_cursor = (battle_setup_action_cursor + 1) % 3;
            if(b || select) battle_setup_actions = false;
            else if(a) {
                const bool selected_active = count > 0 && cursor >= 0 && cursor < count &&
                    game.battle.roster_active[side][cursor];
                // demake disables formation and position changes for a general
                // who has already lost a round. Keep the menu open when this
                // disabled action is confirmed, just like a disabled button.
                if(battle_setup_action_cursor == 1 && !selected_active) break;
                battle_setup_actions = false;
                if(battle_setup_action_cursor == 0) {
                    battle_info_side = side;
                    battle_info_cursor = cursor;
                    screen = Screen::battle_general_info;
                } else if(battle_setup_action_cursor == 1 && selected_active) {
                    game.battle.general_front[side][cursor] ^= 1;
                    battle_info_side = side;
                    battle_info_cursor = cursor;
                    screen = Screen::battle_general_info;
                } else if(battle_setup_action_cursor == 2) {
                    screen = Screen::battle_retreat_confirm;
                    battle_setup_actions = false;
                }
            }
            break;
        }
        if(b) { screen=Screen::battle_retreat_confirm; break; }
        if(select) {
            battle_setup_actions = true;
            battle_setup_action_cursor = 0;
            break;
        }
        if(count > 0) {
            cursor %= count;
            const int direction = up ? -1 : 1;
            if(up || down || !game.battle.roster_active[side][cursor]) {
                for(int step=0;step<count;++step) {
                    cursor = (cursor + count + direction) % count;
                    if(game.battle.roster_active[side][cursor]) break;
                }
            }
            if(side == 0) game.battle.attacker_index = int8_t(cursor);
            else game.battle.defender_index = int8_t(cursor);
            if(a && count > 5) game.battle.layout_front[side][cursor] ^= 1;
            if((l || r) && game.battle.roster_active[side][cursor]) {
                const int general = side == 0 ? game.battle.attackers[cursor] : game.battle.defenders[cursor];
                auto& officer = game.campaign.officers[general];
                for(int step = 0; step < 7; ++step) {
                    const int next = (int(officer.formation) + (r ? step + 1 : 6 - step)) % 7;
                    if((officer.formations >> next) & 1) {
                        officer.formation = uint8_t(next);
                        break;
                    }
                }
            }
        }
        if(start || (a && count <= 5)) {
            if(side == 0) game.battle.attacker_index = int8_t(cursor);
            else game.battle.defender_index = int8_t(cursor);
            game.battle.row_setup_active = 0;
            // demake reloads WarScene after every matchup. WarSceneController.Init
            // restores +10 health and +5 mana for each selected general there.
            // start_duel owns this once-only recovery. The UI must not add
            // another +10/+5 before calling the confirmed initialization.
            game.start_duel();
            screen = Screen::battle;
            skill_slot = 0;
        }
        break;
    }
    case Screen::battle_general_info: {
        const int counts[2] = {game.battle.attacker_count, game.battle.defender_count};
        if(left || right) {
            battle_info_side = 1 - battle_info_side;
            if(counts[battle_info_side] > 0) battle_info_cursor = sg::clamp(battle_info_cursor,0,counts[battle_info_side]-1);
        }
        if(counts[battle_info_side] > 0) {
            if(up) battle_info_cursor = (battle_info_cursor + counts[battle_info_side] - 1) % counts[battle_info_side];
            if(down) battle_info_cursor = (battle_info_cursor + 1) % counts[battle_info_side];
        }
        if(a || b || start || select) screen = Screen::battle_setup;
        break;
    }
    case Screen::battle_retreat_confirm:
        if(a) {
            game.finish_battle(1-player_side());
            battle_setup_actions = false;
            align_phase();
            redraw = true;
        } else if(b || start || select) {
            screen = Screen::battle_setup;
            redraw = true;
        }
        break;
    case Screen::battle_round_result:
        if(a || b || start) {
            game.battle.round_result_pending=0;
            screen=Screen::battle_setup;
            cursor=player_side()==0 ? int(game.battle.attacker_index) : int(game.battle.defender_index);
            redraw=true;
        }
        break;
    case Screen::commands:
        if(select) { screen=Screen::battle_help; break; }
        if(up || down || left || right) game.notice="";
        move_order_menu(up,down,left,right);
        if(a) {
            if(game.issue_battle_command(player_side(),sg::BattleCommand(cursor))) {
                game.battle.paused=0;screen=Screen::battle;game.notice="";
            } else if(!game.notice[0]) game.notice=cursor==2 || cursor==3 ? "前军无法调整，请更换军令" : "当前无法下令，请返回战场";
        }
        if(b || start) { game.battle.paused = 0; screen = Screen::battle; }
        break;
    case Screen::battle_help:
        if(b || select) screen=Screen::commands;
        else if(start) { game.battle.paused=0; screen=Screen::battle; }
        break;
    case Screen::prisoners: {
        int count = prisoner_count();
        if(count) {
            cursor=sg::clamp(cursor,0,count-1);
            if(up) cursor = (cursor+count-1)%count;
            if(down) cursor = (cursor+1)%count;
            if(a) game.handle_prisoner(prisoners[cursor],true);
            if(b) { release_general=prisoners[cursor]; screen=Screen::prisoner_release_confirm; }
        }
        if(start || !prisoner_count()) { game.finish_prisoners(); screen = Screen::map; world_paused = true; }
        break;
    }
    case Screen::battle_report: {
        const int count=game.battle_report.count;
        if(count) {
            if(up) cursor=(cursor+count-1)%count;
            if(down) cursor=(cursor+1)%count;
        }
        if(a || b || start) {
            game.battle_report.pending=0;
            game.notice="";
            screen=Screen::map;
            cursor=0;
            world_paused=true;
            align_phase();
        }
        break;
    }
    case Screen::ended: // input
        if(a || start) {
            ending_frames=0;
            if(player_unified()) {
                screen=Screen::victory_epilogue;
            } else screen=Screen::game_over;
        }
        break;
    case Screen::game_over:
        if(ending_frames >= 120 && (a || start)) return_to_title();
        break;
    case Screen::victory_epilogue:
        if(a || start) { ending_frames=0; screen=Screen::victory_credits; }
        break;
    case Screen::victory_credits:
        // The scroll is decorative. Let the player leave as soon as the roll is
        // on screen instead of forcing ~39 s of waiting after the campaign win.
        if(ending_frames >= 120 && (a || start)) return_to_title();
        break;
    default: break;
    }
}

const sg::SkillDef* officer_current_job(int general) {
    for(int tier=game.skill_count(general)-1;tier>=0;--tier)
        if(const auto* skill=sg::skill_by_id(game.learned_skill(general,tier))) return skill;
    return nullptr;
}

void draw_army_personnel_dossier(sg::Render& render,int general,int selected,int count,
                                int section,bool prisoner) {
    const auto& officer=game.campaign.officers[general];
    static const char* section_names[5]={"武将资料","阵形掌握","兵种掌握","武将技","装备资料"};
    section%=5;
    render.panel(section_names[section]);
    render.heading(12,28,sg::general_defs[general].name,168);
    render.page_marker(selected,count,5);
    if(section==0) {
        const int owner_king=officer.king;
        const bool show_owner=!prisoner && owner_king>=0 &&
            owner_king<sg::scenario_defs[game.campaign.scenario].king_count;
        const char* owner=show_owner ? sg::king_defs[game.campaign.scenario][owner_king].name : "---";
        const auto* job=officer_current_job(general);
        const bool show_job=show_owner && job;
        render.portrait_large(9,31,general);
        render.ui_text(55,48,"势力",sg::muted_ink,28); render.ui_text(86,48,owner,sg::text_ink,48);
        render.ui_text(143,48,"等级",sg::muted_ink,28); render.ui_number(175,48,game.level(general));
        render.ui_text(55,63,"官职",sg::muted_ink,28); render.ui_text(86,63,show_job ? job->title : "---",sg::text_ink,54);
        render.ui_text(143,63,"忠诚",sg::muted_ink,28); render.ui_number(175,63,officer.loyalty);
        render.line(8,78,232,78,sg::rule_ink);
        render.ui_text(12,85,"武力",sg::muted_ink,32); render.ui_number(48,85,game.strength(general));
        render.ui_text(119,85,"智力",sg::muted_ink,32); render.ui_number(155,85,game.intelligence(general));
        render.ui_text(12,101,"体力",sg::muted_ink,32); render.ui_number(48,101,officer.hp);
        render.ui_text(73,101,"/",sg::muted_ink,8); render.ui_number(82,101,game.max_hp(general));
        render.ui_text(119,101,"技力",sg::muted_ink,32); render.ui_number(155,101,officer.mp);
        render.ui_text(180,101,"/",sg::muted_ink,8); render.ui_number(189,101,game.max_mp(general));
        render.ui_text(12,117,"兵力",sg::muted_ink,32); render.ui_number(48,117,officer.soldiers);
        render.ui_text(73,117,"/",sg::muted_ink,8); render.ui_number(82,117,game.max_soldiers(general));
        render.ui_text(12,130,"经验",sg::muted_ink,32);render.ui_unsigned(48,130,officer.exp);
        if(game.level(general)>=50) render.ui_text(142,130,"满级",sg::muted_ink,52);
        else {
            render.ui_text(126,130,"/",sg::muted_ink,8);
            render.ui_unsigned(142,130,game.next_level_experience(general));
        }
    } else if(section==1) {
        render.ui_text(12,48,"当前阵形",sg::muted_ink,60);
        render.ui_text(78,48,sg::ui_formation_name(officer.formation),sg::cinnabar,90);
        for(int i=0;i<7;++i) {
            const int y=65+i*11;
            const bool known=(officer.formations&(1u<<i))!=0;
            render.ui_text(18,y,sg::ui_formation_name(i),known ? sg::text_ink : sg::muted_ink,80);
            render.ui_text(166,y,known ? "已掌握" : "未掌握",known ? sg::text_ink : sg::muted_ink,60);
            if(officer.formation==i) render.ui_text(118,y,"当前",sg::cinnabar,40);
        }
    } else if(section==2) {
        render.ui_text(12,48,"当前兵种",sg::muted_ink,60);
        render.ui_text(78,48,sg::ui_force_name(officer.force),sg::cinnabar,90);
        for(int i=0;i<sg::unit_types;++i) {
            const int col=i/6,row=i%6,x=12+col*116,y=65+row*13;
            const bool known=(officer.forces&(1u<<i))!=0;
            render.ui_text(x,y,sg::ui_force_name(i),
                           officer.force==i ? sg::cinnabar : known ? sg::text_ink : sg::muted_ink,70);
            render.ui_text(x+76,y,known ? "已会" : "未会",known ? sg::text_ink : sg::muted_ink,36);
        }
    } else if(section==3) {
        const int skills=game.skill_count(general);
        if(!skills) render.ui_message(12,61,212,"暂无武将技",sg::muted_ink);
        for(int i=0;i<skills && i<4;++i) {
            const int y=48+i*23;
            const auto* skill=sg::skill_by_id(game.learned_skill(general,i));
            if(!skill) continue;
            render.ui_box(8,y-3,224,19,false);
            render.ui_text(15,y,skill->title,sg::ochre,58);
            render.ui_text(76,y,skill->name,sg::text_ink,105);
            render.ui_text(187,y,"技",sg::muted_ink,14); render.ui_number(204,y,skill->mp);
        }
    } else {
        render.portrait_large(9,34,general);
        render.ui_text(56,43,"武将装备",sg::muted_ink,78);
        const char* equipment=officer.equipped_item ? helper_item_name(officer.equipped_item) : "未装备";
        render.heading(56,61,equipment ? equipment : "未知物品",164);
        render.line(12,83,228,83,sg::rule_ink);
        render.ui_text(16,94,"装备编号",sg::muted_ink,72); render.ui_number(92,94,officer.equipped_item);
        render.ui_text(16,111,"当前兵种",sg::muted_ink,72);
        render.ui_text(92,111,sg::ui_force_name(officer.force),sg::text_ink,100);
        render.ui_text(16,128,"当前阵形",sg::muted_ink,72);
        render.ui_text(92,128,sg::ui_formation_name(officer.formation),sg::text_ink,100);
    }
    render.footer("B返回名单  左右换将  L/R换资料");
}

void draw(sg::Render& render) {
    switch(screen) {
    case Screen::title:
        if(title_scenarios) render.title(sg::scenario_defs[scenario].name,scenario,title_message);
        else render.title_main(title_cursor,title_message,frames);
        break;
    case Screen::save_slots: {
        render.panel(slot_action_save ? "保存记录" : "读取记录");
        // Eight slots remain visible; show only the faction beside each number.
        // Selected metadata gets its own pane instead of overlapping one row.
        for(int i=0;i<8;++i) {
            const auto& slot=slot_summaries[i];
            const int y=30+i*14;
            render.ui_box(7,y,91,13,i==slot_cursor);
            render.ui_number(12,y+2,i+1,sg::ochre);
            if(!(slot_checked_mask&(1<<i))) render.ui_text(29,y+2,"检查中",sg::muted_ink,63);
            else if(slot.valid && slot.scenario<sg::scenario_count && slot.player<sg::max_kings) {
                render.ui_text(29,y+2,sg::king_defs[slot.scenario][slot.player].name,sg::text_ink,63);
            } else render.ui_text(29,y+2,"无记录",sg::muted_ink,63);
        }
        // panel() already painted this area. A flat outline avoids repainting
        // another 14K gradient pixels on every asynchronous loading update.
        render.rect(104,30,129,1,sg::rule_ink);
        render.rect(104,141,129,1,sg::rule_ink);
        render.rect(104,30,1,112,sg::rule_ink);
        render.rect(232,30,1,112,sg::rule_ink);
        render.ui_text(112,34,"记录",sg::muted_ink,27);render.ui_number(143,34,slot_cursor+1,sg::ochre);
        const auto& slot=slot_summaries[slot_cursor];
        const bool checked=(slot_checked_mask&(1<<slot_cursor))!=0;
        if(!checked) {
            render.ui_text(112,52,"正在检查",sg::muted_ink,113);
            render.ui_text(112,88,"可先选择槽位",sg::muted_ink,113);
        } else if(slot.valid && slot.scenario<sg::scenario_count && slot.player<sg::max_kings) {
            render.ui_text(112,52,sg::scenario_defs[slot.scenario].name,sg::text_ink,113);
            render.ui_text(112,70,sg::king_defs[slot.scenario][slot.player].name,sg::ochre,113);
            char date[24];sg::ui::slot_date_label(date,slot.year,slot.month);
            render.ui_text(112,88,date,sg::text_ink,113);
            render.ui_text(112,106,"城池",sg::muted_ink,27);render.ui_number(165,106,slot.cities);
            render.ui_text(112,124,"武将",sg::muted_ink,27);render.ui_number(165,124,slot.generals);
        } else {
            render.ui_text(112,52,"无有效记录",sg::muted_ink,113);
            render.ui_text(112,88,slot_action_save ? "A保存到此槽" : "请选择其他记录",sg::muted_ink,113);
        }
        if(!checked) render.footer("检查中 B/START取消");
        else render.footer(game.notice[0] ? game.notice :
                           slot_action_save ? "上下选择  A保存/覆盖  B返回" : "上下选择  A读取  B返回");
        break;
    }
    case Screen::save_overwrite:
        render.panel("覆盖确认");
        render.ui_message(18,46,204,"要覆盖这条存档记录吗？",sg::cinnabar);
        render.ui_box(18,81,204,35);
        render.ui_text(29,91,"此操作会替换所选槽位",sg::muted_ink,182);
        render.footer("A确认覆盖  B取消");
        break;
    case Screen::title_confirm:
        render.panel("返回标题确认");
        render.ui_message(15,43,210,"返回标题后，未保存的进度会丢失。",sg::cinnabar);
        render.ui_box(15,82,210,40);
        render.ui_message(24,90,192,"建议先取消并保存当前游戏。",sg::text_ink);
        render.footer("A确认离开  B/START取消");
        break;
    case Screen::load_confirm:
        render.panel("读取确认");
        render.ui_message(15,43,210,"读取存档将替换当前未保存的进度。",sg::cinnabar);
        render.ui_box(15,82,210,40);
        render.ui_text(24,91,"即将读取槽位",sg::text_ink,153);
        render.ui_number(178,91,slot_cursor+1,sg::ochre);
        render.footer("A确认读取  B/START取消");
        break;
    case Screen::prisoner_release_confirm:
        render.panel("释放俘虏确认");
        if(release_general>=0) render.heading(16,39,sg::general_defs[release_general].name,208);
        render.ui_message(16,69,208,"释放后武将离开，无法直接收回",sg::cinnabar);
        render.ui_text(16,113,"取消可继续收押或处置",sg::muted_ink,208);
        render.footer("A确认释放  B/START取消");
        break;
    case Screen::kings: {
        render.panel("选择君主"); render.ui_text(8,28,sg::scenario_defs[scenario].name,sg::muted_ink,174);
        int choices[sg::max_kings], count=0, selected=0;
        for(int i=0;i<sg::scenario_defs[scenario].king_count;++i) if(sg::king_defs[scenario][i].selectable) {
            if(i==king) selected=count;
            choices[count++]=i;
        }
        const int first=(selected/5)*5;
        for(int i=first;i<count && i<first+5;++i) render.row(48+(i-first)*18,sg::king_defs[scenario][choices[i]].name,choices[i]==king);
        render.page_marker(selected/5,(count+4)/5,28); render.footer("上下选择  A确认  B返回"); break;
    }
    case Screen::king_confirm: {
        const int ruler=sg::king_defs[scenario][king].general;
        render.panel("确认选择君主");
        render.ui_box(14,34,212,83);
        if(ruler>=0) render.portrait_large(24,43,ruler);
        render.heading(70,43,sg::king_defs[scenario][king].name,140);
        render.ui_text(70,65,sg::scenario_defs[scenario].name,sg::muted_ink,140);
        render.ui_message(24,88,190,"确定以此势力身份开始本局？",sg::text_ink);
        render.footer("A/START开始  B取消");
        break;
    }
    case Screen::year_end_dialogue: {
        render.map(game,city,true,-1,nullptr,map_zoomed,map_speed,false,-1);
        const auto& ruler=sg::king_defs[game.campaign.scenario][game.campaign.player];
        render.ui_box(5,88,230,68);
        render.portrait_large(14,99,ruler.general);
        render.heading(57,93,ruler.name,168);
        render.ui_text(57,112,"内政时间到了……",sg::text_ink,168);
        render.ui_text(120,138,"A/START继续",sg::muted_ink,108);
        break;
    }
    case Screen::map:
        render.map(game,city,world_paused,-1,map_message,map_zoomed,map_speed,false,map_army_focus);
        if(map_army_focus>=0 && game.campaign.armies[map_army_focus].active) {
            const auto& army=game.campaign.armies[map_army_focus];
            render.rect(4,132,232,27,sg::paper);
            if(army.count) render.ui_text(7,133,sg::general_defs[army.generals[0]].name,sg::text_ink,60);
            render.ui_text(70,133,army.garrisoned ? "驻扎中" : army.retreating ? "撤退中" : "行军中",sg::muted_ink,50);
            render.ui_text(125,133,"目标",sg::muted_ink,27);
            render.ui_text(154,133,sg::city_defs[sg::army_destination(game.campaign,map_army_focus)].name,sg::text_ink,76);
            render.ui_text(7,147,"方向/LR选 A命令 B城池",sg::muted_ink,228);
        }
        break;
    case Screen::city: {
        const bool domestic=game.campaign.phase==sg::Phase::domestic;
        const auto& c=game.campaign.cities[city]; const int count=officer_count();
        const bool player_city=c.king==game.campaign.player;
        const bool neutral_city=c.king<0;
        if(!own_selected_city()) {
            int soldiers=0;
            for(int i=0;i<count;++i) soldiers+=game.campaign.officers[officers[i]].soldiers;
            render.panel(sg::city_defs[city].name);
            render.ui_text(158,5,"城池情报",sg::muted_ink,73);
            render.ui_text(12,28,"所属",sg::muted_ink,28);
            render.ui_text(45,28,c.king>=0 ? sg::king_defs[game.campaign.scenario][c.king].name : "无主",sg::cinnabar,100);
            render.ui_text(158,28,c.king>=0 ? "其他势力" : "无主城池",sg::muted_ink,73);
            render.ui_box(8,46,224,40);
            render.portrait_large(14,50,c.major);
            if(c.major<0) render.ui_text(23,59,"无",sg::muted_ink,13);
            render.ui_text(55,49,"太守",sg::muted_ink,28);
            render.ui_text(88,49,c.major>=0 ? sg::general_defs[c.major].name : "暂无",sg::text_ink,132);
            render.ui_text(55,68,"驻将",sg::muted_ink,28);
            if(neutral_city) render.ui_text(88,68,"---",sg::muted_ink); else render.ui_number(88,68,count);
            render.ui_text(128,68,"兵力",sg::muted_ink,28);
            if(player_city) render.ui_number(164,68,soldiers); else render.ui_text(164,68,"---",sg::muted_ink);
            render.ui_box(8,90,224,49);
            render.ui_text(15,94,"资金",sg::muted_ink,28);
            if(player_city) render.ui_amount(49,94,c.money); else render.ui_text(49,94,"---",sg::muted_ink);
            render.ui_text(128,94,"城防",sg::muted_ink,28);
            if(player_city || neutral_city) render.ui_number(174,94,c.defense); else render.ui_text(174,94,"---",sg::muted_ink);
            render.ui_text(15,115,"人口",sg::muted_ink,28);
            if(player_city || neutral_city) render.ui_amount(49,115,c.population); else render.ui_text(49,115,"---",sg::muted_ink);
            render.ui_text(128,115,"预备兵",sg::muted_ink,41);
            if(player_city) render.ui_number(174,115,c.reserve); else render.ui_text(174,115,"---",sg::muted_ink);
            render.footer("B返回地图");
            break;
        }
        render.panel(sg::city_defs[city].name); render.ui_text(160,5,domestic ? "内政" : "大地图",sg::muted_ink);
        render.ui_box(89,27,145,domestic ? 104 : 113);
        const char* actions[]={"出征","征兵","武将资料","俘虏名单","任命太守"};
        const int city_prisoners=domestic ? 0 : city_prisoner_count(city);
        const int ruler=sg::king_defs[game.campaign.scenario][game.campaign.player].general;
        const bool prefect_enabled=ruler>=0 && game.campaign.officers[ruler].city!=city;
        const int worker=count ? officers[officer_cursor%count] : -1;
        for(int i=0;i<(domestic ? city_action_count : world_city_action_count);++i)
            // Six 17px rows end at y128, clear of the two-line footer at132.
            if(domestic) render.city_button(8,27+i*17,i,cursor==i,i>2 || domestic_command_available(i));
            else render.button(8,31+i*20,72,actions[i],cursor==i,
                               (i==3 && !city_prisoners) || (i==4 && !prefect_enabled) ? sg::disabled_ink : sg::text_ink);
        render.city_scene(166,31,c.king!=game.campaign.player);
        render.ui_text(94,30,"金",sg::muted_ink); render.ui_amount(112,30,c.money);
        render.ui_text(94,47,"防",sg::muted_ink); render.ui_number(112,47,c.defense);
        render.ui_text(94,64,"人口",sg::muted_ink); render.ui_amount(137,64,c.population);
        render.ui_text(94,81,"预备兵",sg::muted_ink); render.ui_number(146,81,c.reserve);
        render.line(94,96,228,96,sg::rule_ink);
        if(game.search_result_general>=0 && game.search_result_city==city) {
            render.ui_text(94,99,sg::general_defs[game.search_result_general].name,sg::cinnabar,116);
            render.small_portrait(217,99,game.search_result_general,sg::cinnabar);
            render.ui_message(94,115,132,game.search_result_joined ? "已加入我方" : "拒绝加入我方");
        } else {
            render.ui_text(94,99,"太守",sg::muted_ink);
            render.ui_text(127,99,c.major>=0 ? sg::general_defs[c.major].name : "暂无",sg::text_ink,102);
            render.ui_text(94,117,"经办",sg::muted_ink);
            render.ui_text(127,117,count ? sg::general_defs[worker].name : "没有武将",sg::text_ink,102);
        }
        const char* footer_blocked = domestic && cursor<=2 && !domestic_command_available(cursor) ? "没有符合能力的驻城武将" : nullptr;
        render.footer(game.notice[0] ? game.notice : footer_blocked ? footer_blocked : domestic ? "A确认  B返回  L/R换将" : "A确认  B返回",
                      domestic ? "搜索/筑城/开发每将每年共一次" : nullptr); break;
    }
    case Screen::domestic_generals: {
        const int count=collect_domestic_generals(domestic_action);
        const char* title=domestic_action==2 ? "搜索武将" : domestic_action==1 ? "筑城武将" : "开发武将";
        render.panel(title);
        render.ui_text(12,27,"姓名",sg::muted_ink,72);
        render.ui_text(99,27,"所在城池",sg::muted_ink,61);
        render.ui_text(158,27,"武/智",sg::muted_ink,59);
        const int first=(domestic_roster_cursor/5)*5;
        for(int i=first;i<count && i<first+5;++i) {
            const int general=domestic_generals[i],y=45+(i-first)*17;
            const auto& officer=game.campaign.officers[general];
            const bool disabled=officer.worked!=0;
            const int ink=disabled ? sg::disabled_ink : sg::text_ink;
            render.ui_box(7,y-2,226,17,i==domestic_roster_cursor);
            render.ui_text(12,y,sg::general_defs[general].name,ink,82);
            render.ui_text(98,y,sg::city_defs[officer.city].name,sg::muted_ink,58);
            render.ui_number(158,y,sg::general_defs[general].str,ink);
            render.ui_text(181,y,"/",sg::muted_ink,8);
            render.ui_number(192,y,sg::general_defs[general].intel,ink);
            if(disabled) render.ui_text(217,y,"休",sg::disabled_ink,14);
        }
        if(!count) render.ui_message(12,62,212,"没有符合条件的驻城武将",sg::muted_ink);
        render.page_marker(domestic_roster_cursor/5,(count+4)/5,5);
        render.footer(game.notice[0] ? game.notice : "上下选择  A执行  B返回",
                      "灰字已行动，本年不能重复");
        break;
    }
    case Screen::domestic_result: {
        const char* title=domestic_action==2 ? "搜索结果" : domestic_action==1 ? "筑城结果" : "开发结果";
        render.panel(title);
        if(domestic_result_worker>=0 && domestic_result_worker<sg::general_count &&
           domestic_result_city>=0 && domestic_result_city<sg::city_count) {
            const int worker=domestic_result_worker, result_city=domestic_result_city;
            const auto& c=game.campaign.cities[result_city];
            render.portrait_large(10,34,worker);
            render.heading(52,36,sg::general_defs[worker].name,170);
            render.ui_text(53,57,sg::city_defs[result_city].name,sg::muted_ink,166);
            if(domestic_action==0) {
                render.ui_text(12,78,"人口增加",sg::muted_ink,61);
                render.ui_text(79,78,"+",sg::cinnabar,9);
                render.ui_amount(90,78,domestic_result_delta);
                render.ui_text(12,99,"当前人口",sg::muted_ink,61);
                render.ui_amount(79,99,c.population);
            } else if(domestic_action==1) {
                render.ui_text(12,78,"城防增加",sg::muted_ink,61);
                render.ui_text(79,78,"+",sg::cinnabar,9);
                render.ui_number(90,78,domestic_result_delta);
                render.ui_text(12,99,"当前城防",sg::muted_ink,61);
                render.ui_number(79,99,c.defense,sg::text_ink);
            } else if(game.search_result_general>=0 && game.search_result_city==result_city) {
                const int found=game.search_result_general;
                render.line(8,76,232,76,sg::rule_ink);
                render.portrait_large(10,82,found);
                render.heading(52,84,sg::general_defs[found].name,170);
                render.ui_message(52,105,175,game.search_result_joined ? "人才加入我方" : "人才拒绝加入我方");
            } else if(domestic_result_item>=0) {
                const char* item=city_object_name(uint8_t(domestic_result_item));
                render.line(8,76,232,76,sg::rule_ink);
                render.ui_text(12,88,"发现物品",sg::muted_ink,58);
                render.ui_text(74,88,item ? item : "未知物品",sg::cinnabar,150);
                const auto entry=uint8_t(domestic_result_item);
                const int type=sg::city_inventory_type(entry),index=sg::city_inventory_index(entry);
                const int equipment=12+index;
                const char* bonus=type==1 ? (index<8 ? item_stat_label(equipment) : strength_items_label(equipment)) : nullptr;
                render.ui_text(12,108,type==2 || type==3 ? "使用后：" : "装备后：",sg::muted_ink,61);
                render.ui_text(77,108,type==2 ? "解锁对应兵种" : type==3 ? "解锁对应阵型" :
                    bonus ? bonus : type==1 && index>=27 && index<=30 ? "移动中出手更快" : "特殊装备效果",sg::ochre,150);
                render.ui_text(12,128,"已存入本城物品库",sg::muted_ink,205);
            } else {
                render.line(8,76,232,76,sg::rule_ink);
                render.ui_message(12,91,212,game.notice[0] ? game.notice : "本次没有发现人才或物品",sg::muted_ink);
            }
        } else render.ui_message(12,61,212,game.notice[0] ? game.notice : "内政操作完成",sg::muted_ink);
        render.footer("A/START继续  B回到武将列表");
        break;
    }
    case Screen::officers: {
        const int count=officer_roster_count();
        if(!officer_detail) {
            render.panel(officers_global_roster ? "\u52bf\u529b\u6b66\u5c06" : "\u6b66\u5c06\u540d\u518c");
            render.ui_text(11,27,"姓名",sg::muted_ink); render.ui_text(77,27,"级",sg::muted_ink);
            render.ui_text(102,27,"HP",sg::muted_ink); render.ui_text(132,27,"MP",sg::muted_ink);
            render.ui_text(165,27,"兵种",sg::muted_ink); render.ui_text(210,27,"兵",sg::muted_ink);
            const int first=(officer_cursor/5)*5;
            for(int i=first;i<count && i<first+5;++i) {
                const int g=officers[i], y=45+(i-first)*17; const auto& o=game.campaign.officers[g];
                render.ui_box(6,y-2,228,17,i==officer_cursor);
                render.ui_text(12,y,sg::general_defs[g].name,sg::text_ink,61);
                render.ui_number(77,y,game.level(g)); render.ui_number(102,y,o.hp); render.ui_number(132,y,o.mp);
                render.ui_text(165,y,sg::ui_force_name(o.force),sg::muted_ink,40); render.ui_number(210,y,o.soldiers);
            }
            if(!count) render.ui_message(12,62,212,"本城没有驻扎武将",sg::muted_ink);
            render.page_marker(officer_cursor/5,(count+4)/5,5);
            render.footer(officers_global_roster ? "A\u67e5\u770b  START\u8d44\u6599  B\u8fd4\u56de\u83dc\u5355" : "A\u67e5\u770b  START\u8d44\u6599  B\u8fd4\u56de\u57ce\u5e02", "上下选将 L/R翻页");
        } else {
            render.panel("武将资料");
            if(count) {
                officer_cursor%=count; const int g=officers[officer_cursor]; const auto& o=game.campaign.officers[g];
                render.portrait_large(9,29,g); render.heading(51,28,sg::general_defs[g].name,139);
                render.ui_text(51,48,"等级",sg::muted_ink); render.ui_number(80,48,game.level(g));
                render.ui_text(123,48,"忠诚",sg::muted_ink); render.ui_number(153,48,o.loyalty);
                render.page_marker(officer_cursor,count,5); render.line(7,65,232,65,sg::rule_ink);
                render.ui_text(9,69,"武",sg::muted_ink); render.ui_number(27,69,game.strength(g));
                render.ui_text(66,69,"智",sg::muted_ink); render.ui_number(84,69,game.intelligence(g));
                render.ui_text(122,69,"兵力",sg::muted_ink); render.ui_number(158,69,o.soldiers);
                render.ui_text(9,86,"体力",sg::muted_ink); render.ui_number(44,86,o.hp);
                render.ui_text(120,86,"技力",sg::muted_ink); render.ui_number(155,86,o.mp);
                render.bar(44,99,57,o.hp,game.max_hp(g),sg::cinnabar); render.bar(155,99,57,o.mp,game.max_mp(g),sg::indigo);
                if(game.notice[0]) render.ui_message(9,106,222,game.notice);
                else {
                    render.ui_text(9,105,sg::ui_force_name(o.force),sg::text_ink,59);
                    render.ui_text(72,105,sg::ui_formation_name(o.formation),sg::text_ink,46);
                    const auto* skill=officer_current_job(g);
                    render.ui_text(124,105,skill ? skill->title : "未授官",sg::ochre,108);
                    render.ui_text(9,120,skill ? skill->name : "暂无技能",sg::text_ink,105);
                    const char* equipped=o.equipped_item ? helper_item_name(o.equipped_item) : "未装备";
                    render.ui_text(120,120,equipped ? equipped : "未知物品",sg::muted_ink,110);
                }
            } else render.ui_message(12,61,212,"本城没有可查看的武将",sg::muted_ink);
            render.footer(officers_global_roster ? "L/R\u6362\u5c06  B\u8fd4\u56de\u540d\u518c" : "A\u7269\u54c1  L\u5175\u79cd  R\u9635\u5f62  B\u8fd4\u56de", "\u4e0a\u4e0b\u6362\u5c06  SELECT\u592a\u5b88  START\u540d\u518c");
        }
        break;
    }
    case Screen::items: {
        render.panel(items_global_roster ? "势力物品" : "城池物品"); const int count=item_officer_count();
        if(count) {
            officer_cursor%=count; const int g=officers[officer_cursor]; const auto& o=game.campaign.officers[g];
            render.ui_text(8,28,sg::general_defs[g].name,sg::ochre,100);
            if(items_global_roster) {
                const int location=o.city;
                render.ui_text(112,28,o.army>=0 ? "随军" : location>=0 && location<sg::city_count ?
                    sg::city_defs[location].name : "未驻城",sg::muted_ink,72);
            }
            const char* equipped=o.equipped_item ? helper_item_name(o.equipped_item) : "未装备";
            // The title ends before x=90 and the page marker starts at x=197.
            // Keep gear in that reserved band, separate from the location row.
            render.ui_text(90,5,"装备",sg::muted_ink,26);
            render.ui_text(119,5,equipped ? equipped : "未知装备",sg::text_ink,76);
            const int city_rows=owned_city_item_count(game.campaign,o.king);
            const int legacy_rows=0;
            const int equipped_row=city_rows+legacy_rows;
            const int total=equipped_row+(o.equipped_item ? 1 : 0);
            item_cursor=total ? (item_cursor<total ? item_cursor : total-1) : 0;
            // Reserve feedback space permanently: notices must not repaginate items.
            const int page_size=5, first=(item_cursor/page_size)*page_size;
            for(int i=first;i<total && i<first+page_size;++i) {
                const int y=47+(i-first)*16; const bool is_equipped=i==equipped_row;
                const char* name=nullptr; const char* action=nullptr;
                bool disabled=false;
                if(is_equipped) { name="\u5378\u4e0b\u88c5\u5907"; action="\u5378\u4e0b"; }
                else if(i<city_rows) {
                    int object_city=-1,object_index=-1;
                    if(owned_city_item_location(game.campaign,o.king,i,object_city,object_index)) {
                        const uint8_t entry=sg::city_inventory_item(game.campaign,object_city,object_index);
                        name=city_object_name(entry);
                        disabled=general_has_object(g,sg::city_inventory_type(entry),sg::city_inventory_index(entry));
                        action=disabled ? "\u5df2\u62e5\u6709" : object_city==o.city ? "\u672c\u57ce" : sg::city_defs[object_city].name;
                    }
                } else {
                    const int id=sg::faction_inventory_item(game.campaign,o.king,i-city_rows);
                    name=helper_item_name(id);
                    disabled=faction_item_owned_by_general(g,id);
                    action=disabled ? "\u5df2\u62e5\u6709" : id>=0 && id<55 && sg::original::item_kinds[id]==1 ? "\u4f7f\u7528" : "\u88c5\u5907";
                }
                render.ui_box(6,y-2,228,16,i==item_cursor);
                render.ui_text(14,y,name ? name : "\u672a\u77e5\u7269\u54c1",disabled ? sg::disabled_ink : sg::text_ink,140);
                render.ui_text(158,y,action ? action : "\u4f7f\u7528",disabled ? sg::disabled_ink : sg::muted_ink,67);
            }
            if(!total) render.ui_text(14,58,"\u6ca1\u6709\u7269\u54c1",sg::muted_ink);
            if(game.notice[0]) render.ui_text(8,130,game.notice,sg::warning_ink,224);
            render.page_marker(item_cursor/page_size,(total+page_size-1)/page_size,5);
        } else render.ui_message(12,52,212,"本城没有武将或可用物品",sg::muted_ink);
        render.footer("A用/装 SELECT卸下 L/R换将 B返回"); break;
    }
    case Screen::destination: render.destinations(game,city,cursor,game.notice,map_speed,map_zoomed); break;
    case Screen::assemble: {
        render.panel("出征编组"); const int count=officer_count(), first=(officer_cursor/5)*5;
        render.ui_text(8,28,"\xE7\xBC\x96\xE7\xBB\x84\xE5\x90\x8E\xE9\x80\x89\xE7\x9B\xAE\xE6\xA0\x87\xE5\x9F\x8E\xE6\xB1\xA0",sg::muted_ink,153);
        render.ui_text(143,28,"已选",sg::muted_ink); render.ui_number(175,28,group_count);
        for(int i=first;i<count && i<first+5;++i) {
            const int g=officers[i], y=48+(i-first)*17; bool picked=false;
            for(int j=0;j<group_count;++j) if(group[j]==g) picked=true;
            render.ui_box(6,y-2,228,17,i==officer_cursor);
            render.ui_text(14,y,sg::general_defs[g].name,game.campaign.officers[g].hp ? sg::text_ink : sg::disabled_ink,80);
            render.ui_text(101,y,"兵",sg::muted_ink); render.ui_number(119,y,game.campaign.officers[g].soldiers);
            render.ui_text(173,y,picked ? "已选" : game.campaign.officers[g].hp ? "待选" : "待恢复",
                           picked ? sg::cinnabar : sg::muted_ink,55);
        }
        if(!count) render.ui_text(12,68,"没有可出征武将",sg::muted_ink);
        render.footer("A选择/取消  START出征  B返回",game.notice[0] ? game.notice : nullptr); break;
    }
    case Screen::pause:
        render.panel("暂停菜单");
        for(int i=0;i<pause_action_count();++i) {
            const int columns=pause_columns();
            const int width=columns==3 ? 72 : 104;
            const int gap=columns==3 ? 78 : 112;
            const int x=columns==3 ? 8 : 12;
            const int y=columns==3 ? (game.campaign.phase==sg::Phase::domestic ? 22 : 37) : 30;
            const int row_gap=columns==3 && game.campaign.phase==sg::Phase::domestic ? 22 : columns==3 ? 29 : 27;
            const bool unavailable_promotion=game.campaign.phase==sg::Phase::domestic && i==7 && !promotion_officer_count();
            const bool unavailable_domestic_action=game.campaign.phase==sg::Phase::domestic && i>=3 && i<=5 &&
                !domestic_action_available(5-i);
            const int ink=(unavailable_promotion || unavailable_domestic_action) ? sg::disabled_ink : sg::text_ink;
            render.button(x+(i%columns)*gap,y+(i/columns)*row_gap,width,pause_action(i),cursor==i,ink);
        }
        render.footer("A确认  B返回",game.notice[0] ? game.notice : nullptr); break;
    case Screen::domestic_finish_confirm:
        render.panel("结束内政");
        render.ui_message(13,47,214,"确定结束内政并开始各势力行动？",sg::cinnabar);
        render.footer("A确认结束  B/START取消"); break;
    case Screen::help:
        if(audio_options) {
            render.panel("声音设置");
            render.row(42,"总音量",audio_cursor==0);
            render.ui_number(162,42,sg::audio::volume());
            render.ui_text(181,42,"/4",sg::muted_ink,32);
            render.row(66,"背景音乐",audio_cursor==1);
            render.ui_text(166,66,sg::audio::music_enabled() ? "开" : "关",sg::text_ink,32);
            render.row(90,"操作音效",audio_cursor==2);
            render.ui_text(166,90,sg::audio::effects_enabled() ? "开" : "关",sg::text_ink,32);
            render.ui_message(12,115,216,"设置在本次运行中保留",sg::muted_ink);
            render.footer("上下选 左右调 SELECT说明 B返回");
            break;
        }
        render.panel("操作说明");
        render.ui_text(10,24,"大地图",sg::indigo);
        render.ui_text(12,37,"方向键选城 A查看 B缩放",sg::text_ink,216);
        render.ui_text(12,50,"L/R跳城 L+R部队 SELECT停时",sg::text_ink,216);
        render.ui_text(12,63,"START菜单可选地图速度x1/2/4",sg::text_ink,216);
        render.ui_text(10,78,"城池内政",sg::indigo);
        render.ui_text(12,91,"上下选择 A确认 B返回 L/R换将",sg::text_ink,216);
        render.ui_text(12,104,"SELECT进入声音设置",sg::text_ink,216);
        render.ui_text(10,118,"战斗",sg::indigo);
        render.ui_text(12,131,"A放技 L/R换技 L+R全景",sg::text_ink,216);
        render.footer("滚屏 START指令 SELECT倍速 B复位"); break;
    case Screen::find_generals: {
        render.panel("查找武将");
        const int first=(officer_cursor/5)*5;
        for(int i=first;i<find_general_count && i<first+5;++i) {
            const int general=officers[i], y=47+(i-first)*17;
            const int location=general_map_city(general);
            render.row(y,sg::general_defs[general].name,i==officer_cursor);
            if(location>=0) {
                render.ui_text(112,y,game.campaign.officers[general].city>=0 ? "驻" : "随军",sg::muted_ink,24);
                render.ui_text(137,y,sg::city_defs[location].name,sg::text_ink,86);
            }
        }
        if(!find_general_count) render.ui_message(12,61,212,"当前势力没有可查找的武将。",sg::muted_ink);
        render.page_marker(officer_cursor/5,(find_general_count+4)/5,5);
        render.footer("上下选 L/R翻页 A定位 B返回");
        break;
    }
    case Screen::find_general_confirm: {
        render.panel("定位武将");
        if(find_selected_general>=0) {
            const int general=find_selected_general, location=general_map_city(general);
            render.ui_center(12,44,216,sg::general_defs[general].name,sg::cinnabar);
            if(location>=0) {
                render.ui_center(12,68,216,game.campaign.officers[general].city>=0 ? "驻守城池" : "随军行进至",sg::muted_ink);
                render.ui_center(12,84,216,sg::city_defs[location].name,sg::text_ink);
                render.ui_message(14,105,212,"确认后进入武将所在城池或部队页面",sg::muted_ink);
            } else render.ui_message(14,70,212,"当前无法确定这名武将的位置。",sg::muted_ink);
        }
        render.footer("A前往  B返回列表");
        break;
    }
    case Screen::power_map: {
        render.panel("势力分布");
        const int first=(power_cursor/5)*5;
        for(int i=first;i<power_force_count && i<first+5;++i) {
            const int force=power_forces[i], y=45+(i-first)*18;
            render.row(y,sg::king_defs[game.campaign.scenario][force].name,i==power_cursor);
            render.ui_text(101,y,"城",sg::muted_ink,10); render.ui_number(112,y,force_city_count(force));
            render.ui_text(145,y,"将",sg::muted_ink,10); render.ui_number(156,y,force_general_count(force));
        }
        if(!power_force_count) render.ui_message(12,61,212,"当前没有存续势力。",sg::muted_ink);
        render.page_marker(power_cursor/5,(power_force_count+4)/5,5);
        render.footer("上下选择  A查看城池  B返回");
        break;
    }
    case Screen::power_detail: {
        render.panel("势力城池");
        if(power_force_count) {
            const int force=power_forces[power_cursor], count=force_city_count(force);
            render.ui_text(10,27,"君主",sg::muted_ink);
            render.ui_text(43,27,sg::king_defs[game.campaign.scenario][force].name,sg::cinnabar,53);
            render.ui_text(107,27,"城",sg::muted_ink,13); render.ui_number(124,27,count);
            render.ui_text(151,27,"将",sg::muted_ink,13); render.ui_number(168,27,force_general_count(force));
            const int first=(power_city_cursor/5)*5;
            for(int i=first;i<count && i<first+5;++i) {
                const int target=force_city_at(force,i);
                render.row(48+(i-first)*17,target>=0 ? sg::city_defs[target].name : "无",i==power_city_cursor);
            }
            if(!count) render.ui_message(12,61,212,"该势力当前没有城池。",sg::muted_ink);
            render.page_marker(power_city_cursor/5,(count+4)/5,5);
        }
        render.footer("上下选城 A定位 B返回势力");
        break;
    }
    case Screen::army_commands: {
        render.panel("部队命令");
        if(army_count) {
            const int army_index=army_indices[army_cursor];
            const auto& army=game.campaign.armies[army_index];
            const bool owned=army.king==game.campaign.player;
            const char* actions[]={"武将名单","更改目标","原地驻扎","俘虏名单","进入城池","继续前进"};
            const int nearby_city=game.army_near_city(army_index);
            const bool enabled[]={owned,owned,owned,
                                  owned && count_army_prisoners(army_index)>0,
                                  owned && nearby_city>=0 && nearby_city<sg::city_count &&
                                  game.campaign.cities[nearby_city].king==army.king,
                                  owned && army.garrisoned && army.origin!=army.target};
            for(int i=0;i<6;++i)
                render.button(8,30+i*18,104,actions[i],cursor==i,enabled[i] ? int(sg::white) : int(sg::disabled_ink));
            const int commander=army.count ? army.generals[0] : -1;
            if(commander>=0) render.portrait_large(132,39,commander);
            render.ui_text(127,91,"将领",sg::muted_ink,30); render.ui_number(162,91,army.count);
            render.ui_text(127,108,"路线",sg::muted_ink,30);
            if(army.target>=0 && army.target<sg::city_count)
                render.ui_text(158,108,sg::city_defs[sg::army_destination(game.campaign,army_index)].name,sg::text_ink,72);
            render.ui_text(127,124,game.notice[0] ? game.notice : army.garrisoned ? "驻扎中" : army.retreating ? "撤退中" : "行军中",
                           game.notice[0] ? sg::warning_ink : army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,104);
        }
        render.footer("A确认 SELECT定位 B地图");
        break;
    }
    case Screen::army_target: {
        if(army_count) {
            const auto& army=game.campaign.armies[army_indices[army_cursor]];
            const int current=army.garrisoned || army.remaining*2<=army.total ? army.target : army.origin;
            render.target_cities(game,current,destinations,army_target_count,army_target_cursor,false,game.notice,map_speed,map_zoomed,army_indices[army_cursor]);
        }
        break;
    }
    case Screen::army_prisoners: {
        render.panel("部队俘虏");
        const int first=(army_prisoner_cursor/5)*5;
        for(int i=first;i<army_prisoner_count && i<first+5;++i)
            render.row(45+(i-first)*18,sg::general_defs[army_prisoner_indices[i]].name,i==army_prisoner_cursor);
        render.page_marker(army_prisoner_cursor/5,(army_prisoner_count+4)/5,5);
        if(!army_prisoner_count) render.ui_message(12,61,212,"部队没有携带俘虏",sg::muted_ink);
        render.footer("上下选择 A查看资料 B返回");
        break;
    }
    case Screen::army_prisoner_detail: {
        if(army_prisoner_count) {
            army_prisoner_cursor%=army_prisoner_count;
            const int general=army_prisoner_indices[army_prisoner_cursor];
            draw_army_personnel_dossier(render,general,army_prisoner_cursor,army_prisoner_count,
                                        army_prisoner_info_section,true);
        } else render.ui_message(12,61,212,"部队没有携带俘虏",sg::muted_ink);
        break;
    }
    case Screen::army_general_info: {
        if(army_count) {
            const auto& army=game.campaign.armies[army_indices[army_cursor]];
            if(army.count) {
                army_general_cursor%=army.count;
                const int general=army.generals[army_general_cursor];
                draw_army_personnel_dossier(render,general,army_general_cursor,army.count,
                                            army_prisoner_info_section,false);
            } else render.ui_message(12,61,212,"部队中没有武将",sg::muted_ink);
        }
        break;
    }
    case Screen::city_prisoners: {
        render.panel(sg::city_defs[city].name);
        render.ui_text(154,5,"俘虏名单",sg::muted_ink,73);
        const int count=city_prisoner_count(city);
        const int first=(city_prisoner_cursor/5)*5;
        for(int i=first;i<count && i<first+5;++i) {
            const int general=prisoners[i], y=45+(i-first)*18;
            render.row(y,sg::general_defs[general].name,i==city_prisoner_cursor);
            render.ui_text(177,y,"忠",sg::muted_ink,12);
            render.ui_number(198,y,game.campaign.officers[general].loyalty);
        }
        if(!count) render.ui_message(12,61,212,"本城没有俘虏",sg::muted_ink);
        render.page_marker(city_prisoner_cursor/5,(count+4)/5,5);
        render.footer("上下选择  A查看资料  B返回城池");
        break;
    }
    case Screen::city_prisoner_detail: {
        const int count=city_prisoner_count(city);
        if(count) {
            city_prisoner_cursor%=count;
            const int general=prisoners[city_prisoner_cursor];
            const auto& officer=game.campaign.officers[general];
            render.panel("俘虏资料");
            render.portrait_large(9,31,general);
            render.heading(51,30,sg::general_defs[general].name,138);
            render.page_marker(city_prisoner_cursor,count,5);
            render.line(8,70,232,70,sg::rule_ink);
            render.ui_text(12,77,"等级",sg::muted_ink,30); render.ui_number(47,77,game.level(general));
            render.ui_text(89,77,"武力",sg::muted_ink,30); render.ui_number(124,77,game.strength(general));
            render.ui_text(164,77,"智力",sg::muted_ink,30); render.ui_number(199,77,game.intelligence(general));
            render.ui_text(12,98,"体力",sg::muted_ink,30); render.ui_number(47,98,officer.hp);
            render.ui_text(89,98,"技力",sg::muted_ink,30); render.ui_number(124,98,officer.mp);
            render.ui_text(164,98,"忠诚",sg::muted_ink,30); render.ui_number(199,98,officer.loyalty);
            render.ui_text(12,119,"兵力",sg::muted_ink,30); render.ui_number(47,119,officer.soldiers);
            render.ui_text(89,119,"兵种",sg::muted_ink,30); render.ui_text(124,119,sg::ui_force_name(officer.force),sg::text_ink,55);
            render.ui_text(180,119,sg::ui_formation_name(officer.formation),sg::muted_ink,54);
        } else render.ui_message(12,61,212,"本城没有俘虏",sg::muted_ink);
        render.footer("B返回俘虏名单  左右切换");
        break;
    }
    case Screen::prisoner_persuasion: {
        render.panel("劝降俘虏");
        const int count=prisoner_count();
        const int first=(prisoner_cursor/5)*5;
        for(int i=first;i<count && i<first+5;++i) {
            const int general=prisoners[i], y=45+(i-first)*18;
            const auto& officer=game.campaign.officers[general];
            render.ui_box(6,y-2,228,17,i==prisoner_cursor);
            if(i==prisoner_cursor) render.ui_cursor(13,y+1,sg::cinnabar);
            render.ui_text(24,y,sg::general_defs[general].name,sg::text_ink,84);
            const char* blocked=game.prisoner_persuasion_block(general);
            render.ui_text(166,y,blocked ? blocked : "可劝降",
                           blocked ? sg::muted_ink : sg::text_ink,67);
            render.ui_text(112,y,"忠",sg::muted_ink,13);
            render.ui_number(130,y,officer.loyalty);
        }
        if(!count) render.ui_message(12,61,212,"目前没有可劝降的俘虏",sg::muted_ink);
        render.page_marker(prisoner_cursor/5,(count+4)/5,5);
        render.footer("上下选择  A劝降  B返回暂停",game.notice[0] ? game.notice : nullptr);
        break;
    }
    case Screen::army_list: {
        render.panel("部队列表");
        const int first=(army_cursor/5)*5;
        for(int i=first;i<army_count && i<first+5;++i) {
            const auto& army=game.campaign.armies[army_indices[i]];
            const int y=45+(i-first)*18;
            const int commander=army.count ? army.generals[0] : -1;
            render.row(y,commander>=0 ? sg::general_defs[commander].name : "无将",i==army_cursor);
            render.ui_text(104,y,army.king>=0 ? sg::king_defs[game.campaign.scenario][army.king].name : "无主",
                           army.king==game.campaign.player ? sg::indigo : sg::cinnabar,48);
            render.ui_text(158,y,"将",sg::muted_ink,12); render.ui_number(172,y,army.count);
            render.ui_text(190,y,army.garrisoned ? "驻扎" : army.retreating ? "撤退" : "行军",
                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,36);
        }
        if(!army_count) render.ui_message(12,61,212,"当前没有行军部队",sg::muted_ink);
        render.page_marker(army_cursor/5,(army_count+4)/5,5);
        render.footer("A命令 SELECT定位 B地图");
        break;
    }
    case Screen::army_detail: {
        if(army_count) {
            const auto& army=game.campaign.armies[army_indices[army_cursor]];
            render.panel("部队详情");
            render.ui_text(10,27,"势力",sg::muted_ink,32);
            render.ui_text(44,27,army.king>=0 ? sg::king_defs[game.campaign.scenario][army.king].name : "无主",
                           army.king==game.campaign.player ? sg::indigo : sg::cinnabar,54);
            render.ui_text(111,27,army.garrisoned ? "驻扎中" : army.retreating ? "撤退中" : "行军中",
                           army.garrisoned ? sg::indigo : army.retreating ? sg::cinnabar : sg::muted_ink,50);
            if(army.origin>=0 && army.origin<sg::city_count && army.target>=0 && army.target<sg::city_count) {
                render.ui_text(10,43,sg::city_defs[army.origin].name,sg::text_ink,53);
                render.ui_text(72,43,"→",sg::ochre,16);
                render.ui_text(93,43,sg::city_defs[sg::army_destination(game.campaign,army_indices[army_cursor])].name,sg::text_ink,53);
                render.ui_text(156,43,"剩余",sg::muted_ink,34);
                render.ui_unsigned(194,43,game.army_remaining_travel(army_indices[army_cursor]));
            }
            for(int i=0;i<army.count && i<5;++i) {
                const int general=army.generals[i], y=62+i*16;
                render.ui_box(6,y-2,228,15,i==army_general_cursor);
                render.small_portrait(9,y,general,sg::indigo);
                render.ui_text(31,y,sg::general_defs[general].name,sg::text_ink,61);
                const int soldiers=game.campaign.officers[general].soldiers;
                const int capacity=game.max_soldiers(general);
                render.ui_text(94,y,"兵",sg::muted_ink,13);
                render.ui_number(109,y,soldiers,soldiers*2<capacity ? sg::cinnabar : sg::text_ink);
                render.ui_text(130,y,"/",sg::muted_ink,7);
                render.ui_number(139,y,capacity,sg::muted_ink);
                render.ui_text(169,y,"HP",sg::muted_ink,18); render.ui_number(193,y,game.campaign.officers[general].hp);
            }
            if(!army.count) render.ui_message(12,68,212,"部队中没有武将",sg::muted_ink);
            else render.page_marker(army_general_cursor,army.count,5);
        }
        render.footer("上下选择 A查看资料 B返回命令");
        break;
    }
    case Screen::battle:
        render.combat(game,skill_slot,battle_notice,battle_fit_all);
        render.ui_text(206,146,battle_speed_up ? "速2x" : "速1x",sg::text_ink,34);
        break;
    case Screen::battle_setup: {
        render.panel("选择下一阵出场武将");
        const int player=player_side();
        const int selected[2]={game.battle.attacker_index,game.battle.defender_index};
        const int counts[2]={game.battle.attacker_count,game.battle.defender_count};
        for(int side=0;side<2;++side) {
            const int x=side==0 ? 7 : 124;
            const bool focused=side==player;
            const int count=counts[side];
            const int selection=focused ? cursor : int(selected[side]);
            const int first=(selection/3)*3;
            render.ui_text(x+4,27,side==0 ? "攻方" : "守方",focused ? sg::cinnabar : sg::muted_ink,28);
            if(focused) render.ui_text(x+35,27,"我方",sg::cinnabar,28);
            // At most ten officers: each side needs a real current/total
            // indicator, not an unexplained total that never changes.
            const char pages[]={char('1'+selection/3),'/',char('0'+(count+2)/3),0};
            render.ui_text(x+82,27,count ? pages : "0/0",sg::muted_ink,25);
            for(int i=first;i<count && i<first+3;++i) {
                const int y=44+(i-first)*16;
                const bool available=game.battle.roster_active[side][i]!=0;
                const bool is_selected=available && i==selected[side];
                render.ui_box(x,y-2,108,16,focused && i==cursor);
                render.ui_text(x+4,y,sg::general_defs[side==0 ? game.battle.attackers[i] : game.battle.defenders[i]].name,
                               !available ? sg::muted_ink : is_selected ? sg::cinnabar : sg::text_ink,73);
                if(!available) render.ui_text(x+79,y,"退场",sg::muted_ink,27);
                else if(is_selected) render.ui_text(x+79,y,"出场",sg::cinnabar,27);
                else if(count>5) render.ui_text(x+79,y,game.battle.layout_front[side][i] ? "前列" : "后列",sg::muted_ink,27);
            }
            if(count>0) {
                const int g=side==0 ? game.battle.attackers[selection] : game.battle.defenders[selection];
                const auto& officer=game.campaign.officers[g];
                render.ui_box(x,90,108,42);
                render.ui_text(x+4,93,"武",sg::muted_ink,13);
                render.number(x+18,93,game.strength(g),sg::cinnabar);
                render.ui_text(x+39,93,"智",sg::muted_ink,13);
                render.number(x+53,93,game.intelligence(g),sg::text_ink);
                render.ui_text(x+74,93,"级",sg::muted_ink,13);
                render.number(x+88,93,game.level(g),sg::text_ink);
                render.ui_text(x+4,106,"体",sg::muted_ink,13);
                render.number(x+18,106,officer.hp,sg::text_ink);
                render.ui_text(x+39,106,"技",sg::muted_ink,13);
                render.number(x+53,106,officer.mp,sg::text_ink);
                render.ui_text(x+74,106,"兵",sg::muted_ink,13);
                // Same rule as the army details page: a half-empty formation is
                // the one thing the player must notice before committing.
                render.number(x+88,106,officer.soldiers,
                              officer.soldiers*2<game.max_soldiers(g) ? sg::cinnabar : sg::text_ink);
                render.ui_text(x+4,119,sg::ui_force_name(officer.force),sg::text_ink,41);
                render.ui_text(x+47,119,sg::ui_formation_name(officer.formation),sg::muted_ink,43);
                if(count>5) render.ui_text(x+92,119,game.battle.layout_front[side][selection] ? "前" : "后",sg::muted_ink,13);
            }
        }
        if(battle_setup_actions) {
            render.ui_box(45,45,150,68);
            const char* actions[3]={"\u6b66\u5c06\u8be6\u7ec6\u8d44\u6599","\u6b66\u5c06\u524d\u540e\u4f4d\u7f6e","\u64a4\u9000\u672c\u6b21\u6218\u6597"};
            const int player_index = sg::clamp(cursor,0,sg::max_battle_generals-1);
            const bool position_enabled = counts[player] > 0 && player_index < counts[player] &&
                game.battle.roster_active[player][player_index];
            for(int i=0;i<3;++i) {
                render.ui_box(51,51+i*18,138,17,i==battle_setup_action_cursor);
                const auto color = i==1 && !position_enabled ? sg::muted_ink :
                    i==battle_setup_action_cursor ? sg::cinnabar : sg::text_ink;
                render.ui_text(57,53+i*18,actions[i],color,120);
            }
            render.footer("\u4e0a\u4e0b\u9009\u62e9  A\u786e\u8ba4  B\u8fd4\u56de");
        } else {
            render.footer(counts[player]>5 ? "上下选将 L/R换阵 A前后列" : "上下选将 L/R换阵",
                          counts[player]>5 ? "B撤退 SELECT菜单 START出战" : "A/START出战 B撤退 SELECT菜单");
        }
        break;
    }
    case Screen::battle_general_info: {
        const int count = battle_info_side == 0 ? int(game.battle.attacker_count) : int(game.battle.defender_count);
        const int safe_index = count ? sg::clamp(battle_info_cursor,0,count-1) : 0;
        const int g = count ? (battle_info_side == 0 ? game.battle.attackers[safe_index] : game.battle.defenders[safe_index]) : -1;
        const bool own_side = battle_info_side == player_side();
        render.panel(own_side ? "\u6211\u65b9\u6b66\u5c06\u8d44\u6599" : "\u654c\u65b9\u6b66\u5c06\u8d44\u6599");
        if(g >= 0) {
            const auto& officer = game.campaign.officers[g];
            render.portrait_large(8,30,g);
            render.heading(51,29,sg::general_defs[g].name,140);
            render.ui_text(51,47,"\u6b66\u529b",sg::muted_ink); render.ui_number(84,47,game.strength(g),sg::cinnabar);
            render.ui_text(119,47,"\u667a\u529b",sg::muted_ink); render.ui_number(153,47,game.intelligence(g));
            render.ui_text(181,47,"\u7b49\u7ea7",sg::muted_ink); render.ui_number(215,47,game.level(g));
            render.ui_text(51,63,"HP",sg::muted_ink); render.ui_number(72,63,officer.hp);
            render.ui_text(101,63,"MP",sg::muted_ink); render.ui_number(122,63,officer.mp);
            render.ui_text(153,63,"\u5175\u529b",sg::muted_ink); render.ui_number(190,63,officer.soldiers);
            render.line(8,80,232,80,sg::rule_ink);
            render.ui_text(10,84,sg::ui_force_name(officer.force),sg::text_ink,76);
            render.ui_text(94,84,sg::ui_formation_name(officer.formation),sg::ochre,76);
            render.ui_text(174,84,game.battle.general_front[battle_info_side][safe_index] ? "\u524d\u4f4d" : "\u540e\u4f4d",sg::muted_ink,48);
            const char* equipped=officer.equipped_item ? helper_item_name(officer.equipped_item) : "\u672a\u88c5\u5907";
            render.ui_text(10,99,"\u88c5\u5907",sg::muted_ink); render.ui_text(47,99,equipped ? equipped : "\u672a\u77e5\u7269\u54c1",sg::text_ink,177);
            render.ui_text(10,116,"\u5df2\u638c\u63e1\u6280\u80fd",sg::muted_ink);
            const int skills=game.skill_count(g);
            for(int i=0;i<skills && i<4;++i) {
                const auto* skill=sg::skill_by_id(game.learned_skill(g,i));
                if(skill) render.ui_text(87+(i%2)*75,116+(i/2)*12,skill->name,sg::text_ink,72);
            }
            render.page_marker(safe_index,count,5);
        } else render.ui_message(12,60,212,"\u6ca1\u6709\u53ef\u67e5\u770b\u7684\u6b66\u5c06",sg::muted_ink);
        render.footer("\u4e0a\u4e0b\u6362\u5c06  \u5de6\u53f3\u5207\u6362\u654c\u6211\u65b9  A/B\u8fd4\u56de");
        break;
    }
    case Screen::battle_retreat_confirm:
        render.panel("\u64a4\u9000\u786e\u8ba4");
        render.ui_message(14,46,212,"\u786e\u8ba4\u64a4\u9000\u5e76\u7ed3\u675f\u672c\u6b21\u6218\u6597\uff1f",sg::cinnabar);
        render.ui_box(17,81,206,36);
        render.ui_text(25,90,"\u64a4\u9000\u89c6\u4f5c\u6218\u8d25",sg::muted_ink,190);
        render.footer("A\u786e\u8ba4\u64a4\u9000  B/START\u53d6\u6d88");
        break;
    case Screen::battle_round_result: {
        const auto& battle=game.battle;
        render.panel("\u672c\u573a\u6218\u679c");
        if(battle.round_result_kind==3 || battle.round_result_winner<0 || battle.round_result_loser<0) {
            render.ui_message(14,54,212,"\u672c\u573a\u5bf9\u51b3\u6218\u6210\u5e73\u5c40",sg::ochre);
        } else {
            const int winner=battle.round_result_winner, loser=battle.round_result_loser;
            const auto& winner_info=game.campaign.officers[winner];
            const auto& loser_info=game.campaign.officers[loser];
            const char* loser_result=battle.round_result_kind==2 ? "\u88ab\u4fd8" :
                battle.round_result_kind==4 ? "撤退" : "\u843d\u8d25";
            render.ui_text(10,29,"\u80dc\u8005",sg::ochre,40);
            render.ui_text(131,29,loser_result,battle.round_result_kind==2 ? sg::cinnabar : sg::muted_ink,54);
            // Labels occupy y29..40; portrait frames start two pixels above
            // their origins, so leave a real gap rather than painting over text.
            render.portrait_large(9,45,winner);
            render.heading(53,49,sg::general_defs[winner].name,67);
            render.portrait_large(132,45,loser);
            render.heading(176,49,sg::general_defs[loser].name,58);
            const int before=battle.round_result_level_before;
            const int after=game.level(winner);
            render.ui_text(53,71,before && after>before ? "升级" : "等级",sg::ochre,28);
            render.ui_number(85,71,before && after>before ? before : after,sg::text_ink);
            if(before && after>before) {
                render.ui_text(102,71,">",sg::ochre,8);
                render.ui_number(112,71,after,sg::ochre);
            }
            render.line(8,91,232,91,sg::rule_ink);
            render.ui_text(10,99,"\u6b66\u529b",sg::muted_ink,34); render.ui_number(48,99,game.strength(winner));
            render.ui_text(85,99,"HP",sg::muted_ink,20); render.ui_number(111,99,winner_info.hp);
            render.ui_text(145,99,"MP",sg::muted_ink,20); render.ui_number(171,99,winner_info.mp);
            render.ui_text(10,117,"\u6b66\u529b",sg::muted_ink,34); render.ui_number(48,117,game.strength(loser));
            render.ui_text(85,117,"HP",sg::muted_ink,20); render.ui_number(111,117,loser_info.hp);
            render.ui_text(145,117,"MP",sg::muted_ink,20); render.ui_number(171,117,loser_info.mp);
        }
        render.footer("A/START\u7ee7\u7eed  B\u4e0b\u4e00\u573a\u9009\u5c06");
        break;
    }
    case Screen::commands:
        render.panel("战斗军令");
        if(game.notice[0]) render.ui_text(8,25,game.notice,sg::warning_ink,224);
        else {
            render.ui_text(8,25,"当前",sg::muted_ink);
            render.ui_text(42,25,orders[sg::clamp(int(game.battle.sides[player_side()].order),0,6)],sg::cinnabar);
        }
        {
            bool has_front=false;
            const int side=player_side();
            for(int i=side*200;i<(side+1)*200;++i)
                has_front|=game.battle.soldiers[i].hp>0 && sg::soldier_in_front_rank(game.battle.soldiers[i]);
            for(int i=0;i<8;++i) render.button(7+(i%2)*116,38+(i/2)*25,110,battle_commands[i],cursor==i,
                (i==2 || i==3) && !has_front ? int(sg::disabled_ink) : int(sg::text_ink));
        }
        render.footer("方向选 A下令 B返回 SELECT说明"); break;
    case Screen::battle_help:
        render.panel("战斗操作说明");
        render.ui_text(10,30,"A施放 L/R选技能",sg::text_ink,220);
        render.ui_text(10,46,"L+R全景 / 近景",sg::text_ink,220);
        render.ui_text(10,62,"左右平移 上下看双方",sg::text_ink,220);
        render.ui_text(10,78,"R+上下纵移 B复位",sg::text_ink,220);
        render.ui_text(10,94,"SELECT倍速 START军令",sg::text_ink,220);
        render.ui_text(10,110,"说明页暂停战斗",sg::muted_ink,220);
        render.footer("B/SELECT军令 START战斗"); break;
    case Screen::prisoners: {
        render.panel("处理战俘"); const int count=prisoner_count();
        cursor=count ? sg::clamp(cursor,0,count-1) : 0;
        const int first=(cursor/5)*5;
        render.ui_text(8,28,"待处置武将",sg::muted_ink); render.page_marker(cursor/5,(count+4)/5,5);
        for(int i=first;i<count && i<first+5;++i) render.row(48+(i-first)*17,sg::general_defs[prisoners[i]].name,cursor==i);
        if(!count) render.ui_text(12,61,"没有战俘",sg::muted_ink);
        render.footer("A劝降  B释放  START继续",game.notice[0] ? game.notice : nullptr); break;
    }
    case Screen::troops: {
        render.panel("兵力与阵形"); const int count=officer_count();
        if(count) {
            officer_cursor%=count; const int g=officers[officer_cursor]; const auto& o=game.campaign.officers[g];
            render.portrait_large(9,29,g); render.heading(52,28,sg::general_defs[g].name,170);
            render.ui_text(53,48,sg::ui_force_name(o.force),sg::muted_ink,174);
            render.ui_text(9,70,"总兵力",sg::muted_ink); render.ui_number(64,70,o.soldiers);
            render.ui_text(124,70,"预备兵",sg::muted_ink); render.ui_number(180,70,game.campaign.cities[city].reserve);
            render.ui_text(9,85,"普通",sg::muted_ink); render.ui_number(45,85,o.front);
            render.ui_text(124,85,"精锐",sg::muted_ink); render.ui_number(160,85,int(o.soldiers)-o.front);
            render.ui_text(9,100,"阵形",sg::muted_ink); render.ui_text(45,100,sg::ui_formation_name(o.formation),sg::text_ink,177);
            if(game.notice[0]) render.ui_text(9,117,game.notice,sg::warning_ink,222);
        } else render.ui_text(12,58,"没有可调兵武将",sg::muted_ink);
        render.footer("L/R调兵10 A补满 SELECT兵种","上下换将  B/START结束"); break;
    }
    case Screen::troop_types: {
        const int count=officer_count();
        render.panel("选择兵种");
        if(count) {
            const int general=officers[sg::clamp(officer_cursor,0,count-1)];
            troop_type_count=collect_troop_types(general);
            if(troop_type_count) {
                troop_type_cursor%=troop_type_count;
                const int first=(troop_type_cursor/5)*5;
                render.ui_text(10,27,sg::general_defs[general].name,sg::muted_ink,78);
                render.page_marker(troop_type_cursor/5,(troop_type_count+4)/5,5);
                for(int i=first;i<troop_type_count && i<first+5;++i)
                    render.row(47+(i-first)*17,sg::ui_force_name(troop_type_list[i]),i==troop_type_cursor);
            } else render.ui_text(12,58,"该武将没有可用兵种",sg::muted_ink);
        } else render.ui_text(12,58,"没有可调兵的武将",sg::muted_ink);
        render.footer("上下选择  A确认","B返回征兵页"); break;
    }
    case Screen::promotion: {
        render.panel("晋升官职与技能"); const int count=promotion_officer_count();
        if(count) {
            officer_cursor%=count; const int g=officers[officer_cursor], tier=promotion_tier(g);
            render.ui_text(8,28,sg::general_defs[g].name,sg::ochre);
            if(tier == 3) {
                render.ui_text(121,28,"四十级晋升",sg::muted_ink);
                const auto* skill=sg::skill_by_id(game.promotion_candidate_skill(g,3,0));
                render.row(62,skill ? skill->title : "无",true,sg::text_ink);
                if(skill) render.ui_text(128,62,skill->name,sg::text_ink,96);
            } else if(!tier) {
                render.ui_text(121,28,"已完成晋升",sg::muted_ink);
                render.ui_text(12,58,"该武将没有待晋升技能",sg::disabled_ink);
            } else {
                
                render.ui_text(121,28,tier==1 ? "十级晋升" : "二十级晋升",sg::muted_ink);
                for(int i=0;i<3;++i) {
                    const auto* skill=sg::skill_by_id(game.promotion_candidate_skill(g,tier,i));
                    const int color=game.level(g)>=10 ? sg::text_ink : sg::disabled_ink;
                    const int y=54+i*21;
                    render.row(y,skill ? skill->title : "无",cursor==i,color);
                    if(skill) render.ui_text(128,y,skill->name,cursor==i ? sg::text_ink : color,96);
                }
            }
            render.ui_message(9,117,222,game.notice);
        } else {
            render.ui_text(12,58,"没有可册封武将",sg::muted_ink);
            if(game.notice[0]) render.ui_message(12,82,216,game.notice,sg::ochre);
        }
        render.footer(count ? "A晋升  L/R换将  B返回" : "B返回"); break;
    }
    case Screen::major: {
        render.panel("任命太守"); const int count=officer_count();
        const int page_size=game.notice[0] ? 4 : 5, first=(officer_cursor/page_size)*page_size;
        render.ui_text(8,28,"选择驻城武将",sg::muted_ink);
        for(int i=first;i<count && i<first+page_size;++i) render.row(48+(i-first)*17,sg::general_defs[officers[i]].name,officer_cursor==i);
        if(!count) render.ui_text(12,61,"本城没有驻守武将",sg::muted_ink);
        if(game.notice[0]) render.ui_message(8,114,224,game.notice);
        render.page_marker(officer_cursor/page_size,(count+page_size-1)/page_size,5);
        render.footer("上下选择  A确认  B返回"); break;
    }
    case Screen::major_result: {
        const int prefect=game.campaign.cities[city].major;
        render.panel("太守任命");
        render.ui_box(12,38,216,80);
        render.portrait_large(22,47,prefect);
        render.heading(82,49,prefect>=0 ? sg::general_defs[prefect].name : "暂无",132);
        render.ui_text(82,72,sg::city_defs[city].name,sg::muted_ink,132);
        render.ui_message(82,91,132,"已任命为太守",sg::text_ink);
        render.footer("A/START继续");
        break;
    }
    case Screen::expedition_funds: {
        render.panel("\u51fa\u5f81\u8d44\u91d1");
        const auto& source=game.campaign.cities[city];
        render.ui_text(12,32,"\u57ce\u6c60\u73b0\u6709\u8d44\u91d1",sg::muted_ink,90);
        render.ui_amount(124,32,source.money);
        render.ui_box(13,51,214,51);
        render.ui_text(22,61,"\u968f\u519b\u8d44\u91d1",sg::ochre,78);
        render.ui_amount(122,61,uint32_t(expedition_funds));
        render.ui_text(22,81,"\u7559\u5728\u57ce\u4e2d",sg::muted_ink,78);
        render.ui_amount(122,81,source.money-uint32_t(expedition_funds));
        if(game.notice[0]) render.ui_text(12,113,game.notice,sg::warning_ink,216);
        render.footer("\u65b9\u5411\u952e100  L/R1000  A\u786e\u5b9a B\u8fd4\u56de");
        break;
    }
    case Screen::commander:
        render.panel("前军统领"); render.ui_text(8,28,"选择出征主将",sg::muted_ink);
        for(int i=0;i<group_count;++i) render.row(48+i*17,sg::general_defs[group[i]].name,cursor==i);
        render.footer("上下选择  A确认  B返回",game.notice[0] ? game.notice : nullptr); break;
    case Screen::expedition_abandon_confirm:
        render.panel("\u786e\u8ba4\u5f03\u57ce");
        render.ui_message(13,46,214,"\u5df2\u9009\u51fa\u57ce\u5168\u90e8\u6b66\u5c06\uff0c\u786e\u8ba4\u653e\u5f03\u672c\u57ce\uff1f",sg::cinnabar);
        render.ui_box(17,81,206,36);
        render.ui_text(25,90,"\u653e\u5f03\u540e\u57ce\u6c60\u65e0\u4e3b",sg::muted_ink,190);
        render.footer("A\u786e\u8ba4\u53d1\u5175\u5f03\u57ce  B/START\u53d6\u6d88");
        break;
    case Screen::expedition_prefect: {
        render.panel("\u4efb\u547d\u65b0\u592a\u5b88");
        render.ui_text(10,28,"\u539f\u592a\u5b88\u968f\u519b\u51fa\u5f81\uff0c\u8bf7\u9009\u62e9\u7559\u5b88\u6b66\u5c06",sg::muted_ink,220);
        const int first=(cursor/5)*5;
        for(int i=first;i<expedition_prefect_count && i<first+5;++i) {
            const int general=expedition_prefect_candidates[i];
            render.row(49+(i-first)*18,sg::general_defs[general].name,i==cursor);
            render.ui_text(128,50+(i-first)*18,"\u6b66\u529b",sg::muted_ink,36);
            render.ui_number(166,50+(i-first)*18,game.strength(general),sg::cinnabar);
            render.ui_text(187,50+(i-first)*18,"\u667a\u529b",sg::muted_ink,36);
            render.ui_number(221,50+(i-first)*18,game.intelligence(general),sg::text_ink);
        }
        render.page_marker(cursor,expedition_prefect_count,5);
        render.footer("\u4e0a\u4e0b\u9009\u62e9  A\u4efb\u547d\u5e76\u51fa\u5f81  B\u53d6\u6d88");
        break;
    }
    case Screen::battle_report: {
        const auto& report=game.battle_report;
        render.panel(report.result==0 ? "战役告捷" : report.result==1 ? "战役败北" : "战役平局");
        render.ui_text(174,5,"被俘",sg::cinnabar,28);
        render.ui_number(211,5,report.captured_count,sg::cinnabar);
        render.ui_text(12,28,"我方武将",sg::muted_ink,70);
        render.ui_text(94,28,"处境",sg::muted_ink,40);
        render.ui_text(148,28,"去向/势力",sg::muted_ink,80);
        static const char* outcomes[]={"驻守","随军","撤退中","被俘","失散","已救回"};
        const int first=(cursor/5)*5;
        for(int i=first;i<report.count && i<first+5;++i) {
            const auto& entry=report.entries[i];
            const int y=45+(i-first)*18;
            const bool captured=entry.outcome==sg::BattleOfficerOutcome::captured;
            render.row(y,sg::general_defs[entry.general].name,cursor==i);
            render.ui_text(94,y+1,outcomes[int(entry.outcome)],captured ? sg::cinnabar : sg::text_ink,49);
            const char* whereabouts=captured && entry.king>=0 ? sg::king_defs[game.campaign.scenario][entry.king].name :
                entry.city>=0 && entry.city<sg::city_count ? sg::city_defs[entry.city].name : "途中";
            render.ui_text(148,y+1,whereabouts,captured ? sg::cinnabar : sg::muted_ink,80);
        }
        render.footer("上下查看  A/B确认继续");
        break;
    }
    case Screen::ended:
        if(player_unified()) {
            render.panel("统一天下");
            render.ui_message(14,52,212,"经过多年的征战，统一中国的梦想终于实现了！",sg::text_ink);
            render.footer("A继续");
        } else {
            render.panel("壮志未酬");
            render.ui_message(14,52,212,"很遗憾，统一中国的梦想无法实现了……",sg::text_ink);
            render.footer("A查看结局");
        }
        break;
    case Screen::game_over:
        render.panel("游戏结束");
        // Defeat gets the deep end of the sheen ramp; the ending was previously
        // flat paper with a single line of text.
        render.ui_gradient(0,24,240,136,2,3,1);
        render.rect(0,24,240,1,sg::rule_ink);
        render.ui_center(12,49,216,sg::king_defs[game.campaign.scenario][game.campaign.player].name,sg::cinnabar);
        render.ui_message(14,68,212,"主公战败，本次征战结束。",sg::text_ink);
        render.footer(ending_frames >= 120 ? "A返回标题  10秒后自动返回" : "10秒后自动返回");
        break;
    case Screen::victory_epilogue:
        render.panel("统一中国");
        render.ui_gradient(0,24,240,136,0,2,1);
        render.rect(0,24,240,1,sg::rule_ink);
        render.rect(6,31,228,1,sg::gold_lo);
        render.ui_message(14,48,212,"天下终于归于一统，统一中国的梦想实现了。",sg::text_ink);
        render.ui_text(14,91,"由",sg::muted_ink);
        render.ui_text(32,91,sg::king_defs[game.campaign.scenario][game.campaign.player].name,sg::cinnabar,48);
        render.ui_text(84,91,"统一了全中国。",sg::text_ink,140);
        // A closing seal, the way the original frames its ending panels.
        render.rect(196,112,32,32,sg::edge_dark);
        render.rect(197,113,30,30,sg::cinnabar);
        render.rect(199,115,26,26,sg::seal_ink);
        render.ui_center(199,122,26,"归一",sg::gold_hi);
        render.footer("A播放片尾");
        break;
    case Screen::victory_credits: {
        render.panel("天下归一");
        render.ui_gradient(0,24,240,136,0,2,1);
        render.rect(0,24,240,1,sg::rule_ink);
        render.rect(6,31,228,1,sg::gold_lo);
        const int y=152-int(ending_frames/12);
        render.ui_center(8,y,224,"三国群英传 · 天下归一",sg::text_ink);
        render.ui_center(8,y+20,224,"群雄逐鹿，天下归一",sg::muted_ink);
        render.ui_center(8,y+40,224,"统一中国的梦想至此实现",sg::text_ink);
        render.ui_center(8,y+60,224,"感谢游玩",sg::cinnabar);
        if(ending_frames >= 120) render.footer("A返回标题  10秒后自动返回");
        break;
    }
    default: break;
    }
}
}

int main() {
    bn::core::init();
    sg::audio::init();
    sg::Render render;
    draw(render); render.present(); bn::core::update();
    int simulation_frames = 0;
    uint32_t last_animation = 0;
    while(true) {
        const int elapsed = 1+bn::core::last_missed_frames();
        if(screen == Screen::map) {
            sg_probe[132]=sg::maximum(uint32_t(sg_probe[132]),uint32_t(elapsed-1));
            sg_probe[133]+=unsigned(elapsed-1);
            sg_probe[134]+=1;
        }
        if(screen == Screen::battle) {
            sg_probe[42] = sg::maximum(uint32_t(sg_probe[42]),uint32_t(elapsed-1));
            sg_probe[43] += unsigned(elapsed-1);
            sg_probe[46] = sg::maximum(uint32_t(sg_probe[46]),uint32_t((bn::core::last_cpu_usage()*1000).integer()));
            sg_probe[47] = sg::maximum(uint32_t(sg_probe[47]),uint32_t((bn::core::last_vblank_usage()*1000).integer()));
            if(game.battle.tick > 120) {
                sg_probe[60] = sg::maximum(uint32_t(sg_probe[60]),uint32_t((bn::core::last_cpu_usage()*1000).integer()));
                sg_probe[61] = sg::maximum(uint32_t(sg_probe[61]),uint32_t(elapsed-1));
                sg_probe[62] += unsigned(elapsed-1);
                sg_probe[63] += 1;
            }
        }
        input();
        step_slot_summaries();
        const auto sound_scene=(screen==Screen::title || screen==Screen::kings || screen==Screen::title_confirm) ?
            sg::audio::Scene::menu : (screen==Screen::ended || screen==Screen::game_over ||
            screen==Screen::victory_epilogue || screen==Screen::victory_credits) ? sg::audio::Scene::ending :
            game.campaign.phase==sg::Phase::battle ? sg::audio::Scene::battle : sg::audio::Scene::world;
        sg::audio::update(sound_scene,elapsed);
        if(screen == Screen::game_over || screen == Screen::victory_credits) {
            ending_frames += unsigned(elapsed);
            if((screen == Screen::game_over && ending_frames >= 600) ||
               (screen == Screen::victory_credits && ending_frames >= 2940)) return_to_title();
        }
        if(game.notice != observed_notice) {
            observed_notice = game.notice;
            if(screen == Screen::map) {
                map_message = game.notice[0] ? game.notice : nullptr;
                map_notice_frames = map_message ? 180 : 0;
                redraw = true;
            }
        }
        if(screen != Screen::map) {
            map_message = nullptr;
            map_notice_frames = 0;
        }
        if(map_notice_frames > 0) {
            map_notice_frames = sg::maximum(0,map_notice_frames-elapsed);
            if(map_notice_frames == 0) {
                map_message = nullptr;
                if(screen == Screen::map) redraw = true;
            }
        }
        if(battle_notice_ticks > 0) {
            battle_notice_ticks=sg::maximum(0,battle_notice_ticks-elapsed);
            if(!battle_notice_ticks) { battle_notice=nullptr; redraw=true; }
        }
        frames += unsigned(elapsed);
        simulation_frames += elapsed * (screen == Screen::battle && battle_speed_up ? 2 : 1);
        // Target 30 Hz, but keep input/rendering between battle updates. When
        // a heavy frame exceeds the budget, bursting up to 15 catch-up steps
        // creates more debt and can freeze controls for over a second. Slow
        // the battle clock under overload instead; every model step still runs
        // normally, with explicit 2x mode allowed two steps per presentation.
        if(simulation_frames >= 2) {
            const int update_limit=screen==Screen::battle ? (battle_speed_up ? 2 : 1) : screen==Screen::map ? 1 : 15;
            const int updates = sg::minimum(simulation_frames/2,update_limit);
            simulation_frames %= 2;
            Screen previous_screen = screen;
            auto previous_phase = game.campaign.phase;
            for(int update=0;update<updates;++update) {
                if(screen == Screen::title || screen == Screen::kings) game.advance_menu_random();
                // The strategic clock runs map_speed model steps per simulated
                // frame pair. Stop the moment the phase leaves the map so the
                // domestic/battle handoff below still runs exactly once.
                if(screen == Screen::map && !world_paused)
                    for(int step=0; step<map_speed && game.campaign.phase==sg::Phase::world; ++step)
                        game.world_tick();
                if(screen == Screen::battle) {
                    const auto before = bn::core::current_cpu_usage();
                    const int previous_hp=game.battle.sides[0].hp+game.battle.sides[1].hp;
                    const int previous_soldiers=game.battle.sides[0].soldiers+game.battle.sides[1].soldiers;
                    game.battle_tick();
                    if(game.battle.sides[0].hp+game.battle.sides[1].hp<previous_hp ||
                       game.battle.sides[0].soldiers+game.battle.sides[1].soldiers<previous_soldiers)
                        sg::audio::play(sg::audio::Cue::hit);
                    const auto cost=uint32_t(((bn::core::current_cpu_usage()-before)*1000).integer());
                    sg_probe[49] = sg::maximum(uint32_t(sg_probe[49]),cost);
                    sg_probe[58] += 1; sg_probe[59] += cost;
                }
                align_phase();
            }
            redraw |= previous_screen != screen || previous_phase != game.campaign.phase;
            // Cast-failure feedback belongs to one battle only.
            if(screen != Screen::battle) { battle_notice = nullptr; battle_notice_ticks = 0; }
            // Year-end stops on the ruler's dialogue before opening domestic
            // commands. Keep the model's annual settlement single-shot.
            if(previous_phase==sg::Phase::world && game.campaign.phase==sg::Phase::domestic) {
                screen=Screen::year_end_dialogue;
                cursor=0;
                world_paused=true;
            } else if(game.campaign.phase == sg::Phase::domestic) world_paused = true;
            // Static menus stay in VRAM. Redraw the strategic map at 10 Hz and
            // combat at 30 Hz; simulation is independent of UI refresh.
            bool animated = screen == Screen::title && !title_scenarios ? (frames-last_animation >= 3) :
                screen == Screen::battle ? (frames-last_animation >= 2) :
                (screen == Screen::victory_credits || (screen == Screen::map && !world_paused)) &&
                (frames-last_animation >= (screen==Screen::map ? 6 : 12));
            if(redraw || animated) {
                const auto before = bn::core::current_cpu_usage();
                if(animated && !redraw && screen==Screen::title && !title_scenarios)
                    render.title_leaves(frames,last_animation);
                else draw(render);
                render.present(screen==Screen::battle); redraw = false; last_animation=frames;
                if(screen==Screen::save_slots) slot_page_presented=true;
                if(screen == Screen::battle) {
                    const auto cost=uint32_t(((bn::core::current_cpu_usage()-before)*1000).integer());
                    sg_probe[48] = sg::maximum(uint32_t(sg_probe[48]),cost);
                    sg_probe[56] += 1; sg_probe[57] += cost;
                }
                if(screen == Screen::map) {
                    const auto cost=uint32_t(((bn::core::current_cpu_usage()-before)*1000).integer());
                    sg_probe[71]+=1;
                    sg_probe[72]=sg::maximum(uint32_t(sg_probe[72]),cost);
                    sg_probe[73]+=cost;
                }
            }
            sg_probe[0] = uint32_t(screen);
            sg_probe[69] = uint32_t(domestic_roster_count);
            sg_probe[70] = domestic_roster_count ? uint32_t(domestic_generals[0]) : 0xffffffffu;
            sg_probe[74] = reinterpret_cast<uint32_t>(&city);
            sg_probe[75]=uint32_t(reinterpret_cast<uintptr_t>(&game.battle_report));
            sg_probe[76]=game.battle_report.pending;
            sg_probe[77]=game.battle_report.count;
            sg_probe[78]=game.battle_report.captured_count;
            sg_probe[80]=game.battle.round_result_pending;
            if(game.campaign.phase==sg::Phase::battle) {
                const int lineup_side=player_side();
                const int lineup_index=sg::clamp(cursor,0,sg::max_battle_generals-1);
                sg_probe[81]=game.battle.general_front[lineup_side][lineup_index];
            } else sg_probe[81]=0;
            sg_probe[82]=city>=0 && city<sg::city_count ? uint32_t(int(game.campaign.cities[city].king)+1) : 0;
            sg_probe[83]=city>=0 && city<sg::city_count ? uint32_t(int(game.campaign.cities[city].major)+1) : 0;
            sg_probe[84]=uint32_t(sg::maximum(0,expedition_funds));
            sg_probe[85]=city>=0 && city<sg::city_count ? game.campaign.cities[city].money : 0;
            const int tracked_general=group_count ? group[0] : -1;
            const int tracked_army=tracked_general>=0 ? game.campaign.officers[tracked_general].army : -1;
            sg_probe[86]=tracked_army>=0 && tracked_army<sg::max_armies ? game.campaign.armies[tracked_army].money : 0;
            sg_probe[87]=battle_speed_up ? 1 : 0;
            if(screen==Screen::troops && officer_count()) {
                const int tracked=officers[sg::clamp(officer_cursor,0,officer_count()-1)];
                const auto& officer=game.campaign.officers[tracked];
                sg_probe[88]=officer.soldiers;
                sg_probe[89]=city>=0 && city<sg::city_count ? game.campaign.cities[city].reserve : 0;
                sg_probe[90]=officer.force;
                sg_probe[91]=game.max_soldiers(tracked);
            } else {
                sg_probe[88]=0; sg_probe[89]=0; sg_probe[90]=0; sg_probe[91]=0;
            }
            sg_probe[92]=screen==Screen::army_target && army_target_count ?
                uint32_t(destinations[army_target_cursor]) : 0xffffffffu;
            sg_probe[93]=uint32_t(reinterpret_cast<uintptr_t>(&game.battle.duel_tick));
            sg_probe[94]=game.battle.roster_active_count[0];
            sg_probe[95]=game.battle.roster_active_count[1];
            sg_probe[96]=uint32_t(player_side());
            sg_probe[97]=uint32_t(sg::original::battle_timeout_updates);
            sg_probe[1] = uint32_t(game.campaign.phase);
            sg_probe[2] = game.campaign.scenario;
            sg_probe[3] = game.campaign.player;
            sg_probe[4] = unsigned(city);
            if(city>=0 && city<sg::city_count) {
                sg_probe[125]=uint32_t(int32_t(game.campaign.cities[city].king));
                sg_probe[126]=game.campaign.cities[city].money;
                sg_probe[127]=game.campaign.cities[city].reserve;
            }
            sg_probe[5] = game.campaign.year;
            sg_probe[6] = game.campaign.month_ticks;
            sg_probe[7] = game.battle.tick;
            sg_probe[8] = unsigned(cursor);
            sg_probe[9] = unsigned(group_count);
            sg_probe[10] = frames;
            sg_probe[11] = game.campaign.phase == sg::Phase::battle ? 1u : unsigned(game.valid());
            sg_probe[12] = unsigned(game.battle.attacker_index);
            sg_probe[13] = unsigned(game.battle.defender_index);
            sg_probe[14] = unsigned(game.battle.sides[player_side()].mp);
            sg_probe[15] = unsigned(game.battle.sides[player_side()].hp);
            sg_probe[16] = unsigned(game.battle.sides[player_side()].charge);
            sg_probe[17] = unsigned(game.battle.effect_ticks);
            sg_probe[18] = unsigned(game.battle.effect_remaining);
            const auto* probe_skill = sg::skill_by_id(game.learned_skill(game.battle.sides[player_side()].general, skill_slot));
            sg_probe[19] = unsigned(game.learned_skill(game.battle.sides[player_side()].general, skill_slot));
            sg_probe[20] = probe_skill ? probe_skill->mp : 0;
            sg_probe[21] = unsigned(game.skill_count(game.battle.sides[player_side()].general));
            sg_probe[22] = unsigned(game.campaign.officers[game.battle.sides[player_side()].general].king == game.campaign.player);
            sg_probe[23] = unsigned(game.battle.sides[1 - player_side()].hp);
            sg_probe[24] = unsigned(game.battle.sides[1 - player_side()].soldiers);
            sg_probe[25] = uint32_t(uint16_t(game.search_result_general));
            sg_probe[26] = uint32_t(uint16_t(game.search_result_city));
            sg_probe[27] = uint32_t(game.battle.camera_x);
            sg_probe[28] = game.battle.camera_manual;
            sg_probe[29] = game.search_result_joined;
            sg_probe[30] = reinterpret_cast<uint32_t>(&game.battle.sides[player_side()]);
            sg_probe[31] = reinterpret_cast<uint32_t>(&game.battle.sides[1-player_side()]);
            sg_probe[32] = reinterpret_cast<uint32_t>(&game.battle.soldiers[(1-player_side())*200]);
            sg_probe[33] = reinterpret_cast<uint32_t>(&game.battle.soldiers[player_side()*200]);
            sg_probe[34] = sizeof(game);
            sg_probe[35] = reinterpret_cast<uint32_t>(&game.battle.projectiles[0]);
            sg_probe[36] = reinterpret_cast<uint32_t>(&game.battle.immunity_ticks[0]);
            sg_probe[37] = reinterpret_cast<uint32_t>(&game.battle.effect_remaining);
            sg_probe[38] = reinterpret_cast<uint32_t>(&game.battle.effect_ticks);
            sg_probe[39] = reinterpret_cast<uint32_t>(&game.battle.tick);
            sg_probe[40] = reinterpret_cast<uint32_t>(&game.campaign.random);
            sg_probe[41] = reinterpret_cast<uint32_t>(&game.campaign.random_secondary);
            sg_probe[44] = uint32_t(uint16_t(game.battle.sides[player_side()].x));
            sg_probe[45] = uint32_t(uint16_t(game.battle.camera_y));
            sg_probe[50] = unsigned(map_notice_frames);
            sg_probe[51] = reinterpret_cast<uint32_t>(&game.campaign.phase);
            sg_probe[52] = reinterpret_cast<uint32_t>(&game.campaign.cities[0].king);
            sg_probe[53] = reinterpret_cast<uint32_t>(&game.campaign.cities[1].king);
            sg_probe[54] = sg::city_count;
            sg_probe[55] = game.campaign.armies[0].garrisoned;
            sg_probe[79] = sg::army_destination(game.campaign,0);
            sg_probe[64] = reinterpret_cast<uint32_t>(&game.battle.general_visual[0]);
            sg_probe[65] = sg::battle_view_mirrored(game);
            sg_probe[66] = reinterpret_cast<uint32_t>(&game.battle.camera_x);
            sg_probe[67] = reinterpret_cast<uint32_t>(&game.battle.camera_manual);
            sg_probe[68] = reinterpret_cast<uint32_t>(&game.battle.paused);
            sg_probe[98] = reinterpret_cast<uint32_t>(&game.battle.attackers[0]);
            sg_probe[99] = reinterpret_cast<uint32_t>(&game.battle.defenders[0]);
            sg_probe[100] = reinterpret_cast<uint32_t>(&game.battle.roster_active[0][0]);
            sg_probe[101] = reinterpret_cast<uint32_t>(&game.battle.roster_active_count[0]);
            sg_probe[102] = reinterpret_cast<uint32_t>(&game.battle.attacker_count);
            sg_probe[103] = reinterpret_cast<uint32_t>(&game.battle.defender_count);
            sg_probe[104] = reinterpret_cast<uint32_t>(&game.battle.attacker_index);
            sg_probe[105] = reinterpret_cast<uint32_t>(&game.battle.defender_index);
            sg_probe[106] = reinterpret_cast<uint32_t>(&game.battle.pending_winner);
            sg_probe[107] = reinterpret_cast<uint32_t>(&game.battle.effect_ticks);
            sg_probe[108] = reinterpret_cast<uint32_t>(&game.campaign.officers[0]);
            sg_probe[109] = reinterpret_cast<uint32_t>(&game.campaign.armies[0]);
            // Read-only stage telemetry for real-ROM composition regressions.
            sg_probe[110] = game.battle.magic_composite_kind;
            sg_probe[111] = game.battle.magic_composite_stage;
            sg_probe[112] = uint32_t(int32_t(game.battle.effect_script));
            sg_probe[113] = game.battle.magic_composite_wait;
            sg_probe[114] = game.battle.magic_composite_side;
            sg_probe[128] = uint32_t(map_speed);
            if(screen == Screen::items && item_officer_count()) {
                int g = officers[officer_cursor % item_officer_count()];
                sg_probe[23] = owned_city_item_count(game.campaign,game.campaign.officers[g].king);
                sg_probe[24] = unsigned(game.campaign.officers[g].city);
            } else if(screen == Screen::prisoners) {
                int count = prisoner_count();
                sg_probe[23] = unsigned(count);
                sg_probe[24] = count ? unsigned(prisoners[0]) : 0xffffffffu;
            }
        }
        bn::core::update();
    }
}
