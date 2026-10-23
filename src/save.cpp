#include "save.h"
#include "original_rules.h"
#include <stddef.h>
#include <stdint.h>

namespace sg {
namespace {
constexpr uint32_t magic = 0x53475131;
constexpr uint16_t version = 17;
// Version 1 is reserved as the unlock-bearing form of the current record.
// Its byte count distinguishes it from the tiny marker in an unused slot.
constexpr uint16_t unlocked_version = 1;
constexpr uint32_t game_pass_marker = 0x47504153;
constexpr int slot_size = 16384;
constexpr int slot_count = 8;
constexpr int flash_bank_size = 0x10000;
constexpr int flash_sector_size = 0x1000;
constexpr uint32_t flash_base = 0x0E000000;
constexpr uint16_t flash_unlock_hi = 0x5555;
constexpr uint16_t flash_unlock_lo = 0x2AAA;
constexpr uint8_t flash_cmd_start = 0xAA;
constexpr uint8_t flash_cmd_continue = 0x55;
constexpr uint8_t flash_cmd_program = 0xA0;
constexpr uint8_t flash_cmd_erase = 0x80;
constexpr uint8_t flash_cmd_erase_sector = 0x30;
constexpr uint8_t flash_cmd_switch_bank = 0xB0;

struct Header { uint32_t magic_value; uint16_t version_value, bytes; uint32_t sequence, checksum; };
struct LegacyHeader { uint32_t magic_value, version_value, bytes, sequence, checksum; };
struct Record { Header header; Campaign campaign; };
__attribute__((section(".ewram"))) Record scratch;
__attribute__((section(".ewram"))) Game validator;
constexpr unsigned legacy_v8_v9_bytes = offsetof(Campaign,random_secondary);
constexpr unsigned legacy_v10_bytes = offsetof(Campaign,faction_experience);
constexpr unsigned legacy_v11_bytes = sizeof(Campaign);
static_assert(sizeof(Record) == slot_size);

volatile uint8_t* const flash_window = reinterpret_cast<volatile uint8_t*>(flash_base);
int active_flash_bank = -1;

uint8_t flash_read(uint32_t offset) {
    const int bank = int(offset / flash_bank_size);
    if(bank != active_flash_bank) {
        flash_window[flash_unlock_hi] = flash_cmd_start;
        flash_window[flash_unlock_lo] = flash_cmd_continue;
        flash_window[flash_unlock_hi] = flash_cmd_switch_bank;
        flash_window[0] = uint8_t(bank);
        active_flash_bank = bank;
    }
    return flash_window[offset & (flash_bank_size - 1)];
}

void select_flash_bank(uint32_t offset) {
    const int bank = int(offset / flash_bank_size);
    if(bank != active_flash_bank) {
        flash_window[flash_unlock_hi] = flash_cmd_start;
        flash_window[flash_unlock_lo] = flash_cmd_continue;
        flash_window[flash_unlock_hi] = flash_cmd_switch_bank;
        flash_window[0] = uint8_t(bank);
        active_flash_bank = bank;
    }
}

void flash_read_bytes(void* destination, uint32_t offset, unsigned bytes) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    while(bytes) {
        select_flash_bank(offset);
        const unsigned bank_bytes = flash_bank_size - (offset & (flash_bank_size - 1));
        const unsigned chunk = bytes < bank_bytes ? bytes : bank_bytes;
        volatile const uint8_t* source = flash_window + (offset & (flash_bank_size - 1));
        for(unsigned i = 0; i < chunk; ++i) output[i] = source[i];
        offset += chunk; output += chunk; bytes -= chunk;
    }
}

void flash_wait(uint32_t offset, uint8_t expected) {
    while((flash_read(offset) & 0x80) != (expected & 0x80)) { }
}

void flash_erase_sector(uint32_t offset) {
    const int bank = int(offset / flash_bank_size);
    flash_read(offset); // Ensure the address window is switched before issuing a command.
    flash_window[flash_unlock_hi] = flash_cmd_start;
    flash_window[flash_unlock_lo] = flash_cmd_continue;
    flash_window[flash_unlock_hi] = flash_cmd_erase;
    flash_window[flash_unlock_hi] = flash_cmd_start;
    flash_window[flash_unlock_lo] = flash_cmd_continue;
    flash_window[offset & (flash_bank_size - 1)] = flash_cmd_erase_sector;
    (void) bank;
    flash_wait(offset,0xFF);
}

void flash_program_byte(uint32_t offset, uint8_t value) {
    flash_read(offset); // Select the target 64 KiB flash bank.
    flash_window[flash_unlock_hi] = flash_cmd_start;
    flash_window[flash_unlock_lo] = flash_cmd_continue;
    flash_window[flash_unlock_hi] = flash_cmd_program;
    flash_window[offset & (flash_bank_size - 1)] = value;
    flash_wait(offset,value);
}

uint32_t checksum_bytes(const uint8_t* data, unsigned bytes) {
    uint32_t result = 2166136261u;
    for(unsigned i = 0; i < bytes; ++i) result = (result ^ data[i]) * 16777619u;
    return result;
}

uint32_t checksum_flash(uint32_t offset, unsigned bytes) {
    uint32_t result = 2166136261u;
    while(bytes) {
        select_flash_bank(offset);
        const unsigned bank_bytes = flash_bank_size - (offset & (flash_bank_size - 1));
        const unsigned chunk = bytes < bank_bytes ? bytes : bank_bytes;
        volatile const uint8_t* source = flash_window + (offset & (flash_bank_size - 1));
        for(unsigned i = 0; i < chunk; ++i) result = (result ^ source[i]) * 16777619u;
        offset += chunk; bytes -= chunk;
    }
    return result;
}

bool read_slot(int slot, Header& header) {
    if(slot < 0 || slot >= slot_count) return false;
    const uint32_t slot_offset = uint32_t(slot * slot_size);
    LegacyHeader old_header{};
    flash_read_bytes(&old_header,slot_offset,sizeof(old_header));
    const bool old_layout = old_header.magic_value == magic &&
        (old_header.version_value == 8 || old_header.version_value == 9 || old_header.version_value == 10);
    const unsigned payload_offset = old_layout ? sizeof(LegacyHeader) : sizeof(Header);
    if(old_layout) {
        header = {old_header.magic_value,uint16_t(old_header.version_value),uint16_t(old_header.bytes),
                  old_header.sequence,old_header.checksum};
    } else {
        flash_read_bytes(&header,slot_offset,sizeof(header));
    }
    if(header.magic_value != magic) return false;
    if(header.version_value==unlocked_version && header.bytes==0 &&
       header.checksum==game_pass_marker) return false;
    const bool legacy_v8_v9 = old_layout && (header.version_value == 8 || header.version_value == 9);
    const bool legacy_v10 = old_layout && header.version_value == 10;
    const bool legacy_v11 = !old_layout && header.version_value == 11;
    const bool legacy_v12 = !old_layout && header.version_value == 12;
    const bool legacy_v13 = !old_layout && header.version_value == 13;
    const bool legacy_v14 = !old_layout && header.version_value == 14;
    const bool legacy_v15 = !old_layout && header.version_value == 15;
    const bool legacy_v16 = !old_layout && header.version_value == 16;
    const bool unlocked_current = !old_layout && header.version_value==unlocked_version &&
        header.bytes==sizeof(Campaign);
    const bool legacy = legacy_v8_v9 || legacy_v10 || legacy_v11 || legacy_v12 || legacy_v13 ||
                        legacy_v14 || legacy_v15 || legacy_v16;
    if((!legacy && header.version_value != version && !unlocked_current) ||
       (legacy_v8_v9 && header.bytes != legacy_v8_v9_bytes) ||
       (legacy_v10 && header.bytes != legacy_v10_bytes) ||
       ((legacy_v11 || legacy_v12 || legacy_v13 || legacy_v14 || legacy_v15 || legacy_v16) &&
        header.bytes != legacy_v11_bytes) ||
       (!legacy && header.bytes != sizeof(Campaign))) return false;
    scratch.campaign = {};
    flash_read_bytes(&scratch.campaign,slot_offset + payload_offset,header.bytes);
    if(legacy) {
        if(checksum_bytes(reinterpret_cast<const uint8_t*>(&scratch.campaign),header.bytes) != header.checksum) return false;
        if(legacy_v8_v9) {
            scratch.campaign.random_secondary = scratch.campaign.random ^ 0x87654321u;
            if(scratch.campaign.random_secondary == 0) scratch.campaign.random_secondary = 0x87654321u;
        }
        if(legacy_v8_v9 || legacy_v10) {
            for(uint32_t& experience : scratch.campaign.faction_experience) experience = 0;
        }
        // Only the pre-v12 layouts lack persisted growth caps. v16 carries
        // the same learned HP/MP caps as v12-v15; recomputing them can lower
        // a legitimate officer's maximum below their saved current health.
        if(legacy_v8_v9 || legacy_v10 || legacy_v11) for(int i = 0; i < general_count; ++i) {
            auto& officer = scratch.campaign.officers[i];
            const int item = officer.equipped_item;
            set_officer_level_hp_cap(officer,uint16_t(original::max_hp(
                general_defs[i].hp,int(general_defs[i].str)+original::item_strength_bonus(item),officer.rank)));
            set_officer_level_mp_cap(officer,uint16_t(original::max_mp(
                general_defs[i].mp,int(general_defs[i].intel)+original::item_intelligence_bonus(item),officer.rank)));
        }
    }
    if((legacy_v8_v9 || legacy_v10 || legacy_v11 || legacy_v12) &&
       !migrate_personal_items(scratch.campaign)) return false;
    if(legacy_v13 || legacy_v14 || header.version_value == 15) migrate_faction_items(scratch.campaign);
    if(legacy) for(int i = 0; i < general_count; ++i) {
        const auto& officer = scratch.campaign.officers[i];
        const int initial_king = scratch.campaign.scenario < scenario_count ?
            general_initial[scratch.campaign.scenario][i].king : -1;
        if(!legacy_v16) {
            native_origin_king(scratch.campaign,i) = officer.status == Status::prisoner || initial_king < 0 ?
                0xff : uint8_t(initial_king);
            native_status2_countdown(scratch.campaign,i) = general_defs[i].constancy;
        }
        set_native_escape_count(scratch.campaign,i,0);
    }
    if(scratch.campaign.phase != Phase::world && scratch.campaign.phase != Phase::domestic) return false;
    validator.campaign = scratch.campaign;
    const bool valid_campaign = validator.valid();
    const bool valid_checksum = legacy || checksum_flash(slot_offset + payload_offset,header.bytes) == header.checksum;
    return valid_campaign && valid_checksum;
}

bool write_campaign_record(const Campaign& campaign,int slot,uint32_t sequence,uint16_t record_version) {
    if(slot<0 || slot>=slot_count) return false;
    scratch.campaign=campaign;
    scratch.header={magic,record_version,uint16_t(sizeof(Campaign)),sequence,
                    checksum_bytes(reinterpret_cast<const uint8_t*>(&scratch.campaign),sizeof(Campaign))};
    const uint32_t slot_offset=uint32_t(slot*slot_size);
    for(uint32_t offset=slot_offset;offset<slot_offset+slot_size;offset+=flash_sector_size)
        flash_erase_sector(offset);
    const uint32_t payload_offset=slot_offset+uint32_t(offsetof(Record,campaign));
    const uint8_t* payload=reinterpret_cast<const uint8_t*>(&scratch.campaign);
    for(unsigned i=0;i<sizeof(Campaign);++i) if(payload[i]!=0xFF)
        flash_program_byte(payload_offset+i,payload[i]);
    if(checksum_flash(payload_offset,sizeof(Campaign))!=scratch.header.checksum) return false;
    const uint8_t* header_bytes=reinterpret_cast<const uint8_t*>(&scratch.header);
    for(unsigned i=0;i<sizeof(Header);++i) if(header_bytes[i]!=0xFF)
        flash_program_byte(slot_offset+i,header_bytes[i]);
    Header verified{};
    return read_slot(slot,verified) && verified.sequence==sequence;
}

bool slot_is_erased(int slot) {
    const uint32_t offset=uint32_t(slot*slot_size);
    for(unsigned i=0;i<slot_size;++i) if(flash_read(offset+i)!=0xFF) return false;
    return true;
}

bool write_game_pass_marker(int slot) {
    Header marker{magic,unlocked_version,0,0,game_pass_marker};
    const uint8_t* bytes=reinterpret_cast<const uint8_t*>(&marker);
    const uint32_t offset=uint32_t(slot*slot_size);
    for(uint32_t sector=offset;sector<offset+slot_size;sector+=flash_sector_size)
        flash_erase_sector(sector);
    for(unsigned i=0;i<sizeof(marker);++i) if(bytes[i]!=0xFF)
        flash_program_byte(offset+i,bytes[i]);
    Header verified{};
    flash_read_bytes(&verified,offset,sizeof(verified));
    return verified.magic_value==magic && verified.version_value==unlocked_version &&
           verified.bytes==0 && verified.checksum==game_pass_marker;
}

void load_into_game(Game& game) {
    game.campaign = scratch.campaign;
    game.battle = {};
    game.battle_report = {};
    game.search_result_general = -1;
    game.search_result_joined = 0;
    game.search_result_city = -1;
    game.notice = "读档完成";
}
}

bool save_game(const Game& game, int slot) {
    if(slot < 0 || slot >= slot_count || !game.valid() ||
       (game.campaign.phase != Phase::world && game.campaign.phase != Phase::domestic)) return false;
    uint32_t sequence = 0;
    Header existing{};
    for(int i = 0; i < slot_count; ++i) if(read_slot(i,existing) && existing.sequence >= sequence)
        sequence = existing.sequence + 1;
    return write_campaign_record(game.campaign,slot,sequence,
                                 game_pass_unlocked()?unlocked_version:version);
}

bool save_game(const Game& game) { return save_game(game,0); }

bool game_pass_unlocked() {
    for(int slot=0;slot<slot_count;++slot) {
        Header header{};
        const uint32_t offset=uint32_t(slot*slot_size);
        flash_read_bytes(&header,offset,sizeof(header));
        if(header.magic_value==magic && header.version_value==unlocked_version &&
           header.bytes==0 && header.checksum==game_pass_marker) return true;
        // The unlock bit is committed in the record header only after the
        // campaign payload has been written and checksummed. Avoid re-reading
        // and validating every 16 KiB save when title flow only needs this bit.
        if(header.magic_value==magic && header.version_value==unlocked_version &&
           header.bytes==sizeof(Campaign)) return true;
    }
    return false;
}

bool unlock_game_pass() {
    if(game_pass_unlocked()) return true;
    uint32_t newest_sequence=0;
    int rewrite_slot=-1,blank_slot=-1,reclaim_slot=-1;
    Header header{};
    for(int slot=0;slot<slot_count;++slot) {
        if(read_slot(slot,header)) {
            if(header.sequence>=newest_sequence) newest_sequence=header.sequence+1;
            // read_slot leaves this record in validator, so keep the slot and
            // campaign from the same (last valid) record for the full-flash path.
            rewrite_slot=slot;
        } else if(blank_slot<0 && slot_is_erased(slot)) blank_slot=slot;
        else if(reclaim_slot<0) reclaim_slot=slot;
    }
    if(blank_slot>=0) return write_game_pass_marker(blank_slot);
    // A corrupt/obsolete slot can be reclaimed without touching any valid save.
    if(reclaim_slot>=0) return write_game_pass_marker(reclaim_slot);
    // If all slots contain valid saves, retain the newest campaign snapshot
    // loaded by read_slot and rewrite its slot in the unlock-bearing format.
    if(rewrite_slot>=0)
        return write_campaign_record(validator.campaign,rewrite_slot,newest_sequence,unlocked_version);
    // No valid record exists and flash is full of invalid data. Preserve the
    // unlock in slot 0; the next save replaces this marker with a full record.
    for(uint32_t offset=0;offset<slot_size;offset+=flash_sector_size)
        flash_erase_sector(offset);
    return write_game_pass_marker(0);
}

bool load_game_slot(Game& game, int slot) {
    Header header{};
    if(!read_slot(slot,header)) return false;
    load_into_game(game);
    return true;
}

bool save_slot_info(int slot, uint8_t& scenario, uint8_t& player, uint8_t& cities,
                    uint8_t& generals, uint16_t& year, uint8_t& month, uint32_t& sequence) {
    Header header{};
    if(!read_slot(slot,header)) return false;
    scenario = scratch.campaign.scenario; player = scratch.campaign.player;
    year = scratch.campaign.year; month = scratch.campaign.month; sequence = header.sequence;
    cities = generals = 0;
    for(int i = 0; i < city_count; ++i)
        if(scratch.campaign.cities[i].king == player) ++cities;
    for(int i = 0; i < general_count; ++i) {
        const auto& officer = scratch.campaign.officers[i];
        if(officer.status == Status::employed && officer.king == player) ++generals;
    }
    return true;
}

bool load_game(Game& game) {
    int newest = -1;
    uint32_t newest_sequence = 0;
    Header header{};
    for(int i = 0; i < slot_count; ++i)
        if(read_slot(i,header) && (newest < 0 || header.sequence > newest_sequence)) {
            newest = i; newest_sequence = header.sequence;
        }
    return newest >= 0 && load_game_slot(game,newest);
}
}
